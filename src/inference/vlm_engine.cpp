#include "vlm_engine.h"

// ── llama.cpp + mtmd (多模态) C API 头文件 ──
#include <llama.h>
#include <mtmd.h>
#include <mtmd-helper.h>

#include <cstring>
#include <algorithm>
#include <sstream>
#include <chrono>
#include <filesystem>

namespace navi {

// ============================================================
//  系统提示词模板
//  强制模型输出严格的 JSON 结构，与 GameStateData 字段对齐
// ============================================================
static const char* SYSTEM_PROMPT = R"(You are a game vision AI assistant. Analyze the given game screenshot and respond with ONLY a valid JSON object in this exact format:
{"current_status":"safe|combat|exploring|retreating|looting","detected_units":["unit1","unit2"],"tactical_advice":"brief tactical suggestion","confidence":0.85}
Rules:
- current_status must be one of: safe, combat, exploring, retreating, looting
- detected_units is an array of visible game entities/characters
- tactical_advice is a brief one-sentence suggestion
- confidence is a float between 0.0 and 1.0
Respond with ONLY the JSON object, no markdown fences, no explanation.)";

// ============================================================
//  构造函数 — 加载模型
// ============================================================
VlmEngine::VlmEngine(const VlmConfig& config)
    : config_(config)
{
    // 初始化 llama.cpp 后端（全局只需调用一次，内部有保护）
    llama_backend_init();

    if (!loadModels()) {
        // 加载失败时错误信息已通过 setError 记录
        unloadModels();
    }
}

// ============================================================
//  析构函数 — 释放所有 llama.cpp 资源
// ============================================================
VlmEngine::~VlmEngine() {
    unloadModels();
    // 注意：不调用 llama_backend_free()，因为其他实例可能仍在使用
}

// ============================================================
//  模型加载（双模型：主 LLM + CLIP 视觉投影器）
// ============================================================
bool VlmEngine::loadModels() {
    // ── 检查文件是否存在 ──
    if (!std::filesystem::exists(config_.model_path)) {
        setError("主模型文件不存在: " + config_.model_path);
        return false;
    }
    if (!std::filesystem::exists(config_.mmproj_path)) {
        setError("视觉投影器文件不存在: " + config_.mmproj_path);
        return false;
    }

    // ── 1. 加载主 LLM 模型 ──
    llama_model_params model_params = llama_model_default_params();
    model_params.n_gpu_layers = config_.n_gpu_layers;
    model_params.use_mmap     = true;

    model_ = llama_model_load_from_file(config_.model_path.c_str(), model_params);
    if (!model_) {
        setError("llama_model_load_from_file 失败，可能原因：文件损坏、VRAM 不足、格式不兼容");
        return false;
    }

    // ── 2. 创建推理上下文 ──
    llama_context_params ctx_params = llama_context_default_params();
    ctx_params.n_ctx     = config_.n_ctx;
    ctx_params.n_threads = config_.n_threads;
    ctx_params.n_batch   = config_.n_batch;

    ctx_ = llama_new_context_with_model(model_, ctx_params);
    if (!ctx_) {
        setError("llama_new_context_with_model 失败，可能 n_ctx 过大导致内存分配失败");
        return false;
    }

    // ── 3. 加载 mtmd 多模态上下文（替代旧版 CLIP + LLaVA） ──
    mtmd_context_params mparams = mtmd_context_params_default();
    mparams.use_gpu       = (config_.n_gpu_layers > 0);
    mparams.print_timings = false;
    mparams.n_threads     = config_.n_threads;

    mtmdCtx_ = mtmd_init_from_file(config_.mmproj_path.c_str(), model_, mparams);
    if (!mtmdCtx_) {
        setError("mtmd_init_from_file 失败，视觉投影器文件可能损坏或与主模型不兼容");
        return false;
    }

    // ── 4. 创建采样链：temp → top_k → top_p → dist ──
    llama_sampler_chain_params sparams = llama_sampler_chain_default_params();
    sampler_ = llama_sampler_chain_init(sparams);
    llama_sampler_chain_add(sampler_, llama_sampler_init_temp(config_.temperature));
    llama_sampler_chain_add(sampler_, llama_sampler_init_top_k(40));
    llama_sampler_chain_add(sampler_, llama_sampler_init_top_p(0.95f, 1));
    llama_sampler_chain_add(sampler_, llama_sampler_init_dist(42));

    loaded_.store(true);
    return true;
}

// ============================================================
//  资源释放
// ============================================================
void VlmEngine::unloadModels() {
    loaded_.store(false);

    if (sampler_) {
        llama_sampler_free(sampler_);
        sampler_ = nullptr;
    }
    if (mtmdCtx_) {
        mtmd_free(mtmdCtx_);
        mtmdCtx_ = nullptr;
    }
    if (ctx_) {
        llama_free(ctx_);
        ctx_ = nullptr;
    }
    if (model_) {
        llama_model_free(model_);
        model_ = nullptr;
    }
}

// ============================================================
//  IInferenceEngine::analyze_frame 实现
//
//  线程安全：内部通过 inferenceMutex_ 保证串行执行
//  调用侧（AppGui）已将其放在后台线程，不会阻塞 UI
// ============================================================
std::string VlmEngine::analyze_frame(
    const std::vector<uint8_t>& image_data,
    int width, int height,
    const std::string& voice_text_prompt)
{
    if (!loaded_.load()) {
        return makeFallbackJson("模型未加载: " + last_error());
    }

    // 推理互斥锁 — llama.cpp 上下文非线程安全，同一时刻只能有一个推理在执行
    std::lock_guard<std::mutex> lock(inferenceMutex_);

    try {
        return doInference(image_data, width, height, voice_text_prompt);
    } catch (const std::exception& e) {
        setError(std::string("推理异常: ") + e.what());
        return makeFallbackJson(e.what());
    } catch (...) {
        setError("推理过程发生未知异常");
        return makeFallbackJson("unknown error");
    }
}

// ============================================================
//  核心推理流程
//
//  流程：清空 KV → eval 系统提示 → eval 图像 embedding → eval 用户提示 → 自回归生成
// ============================================================
std::string VlmEngine::doInference(
    const std::vector<uint8_t>& bgra_pixels,
    int width, int height,
    const std::string& user_prompt)
{
    // ── 0. 清除上一轮的 KV 缓存，开始全新推理 ──
    llama_memory_clear(llama_get_memory(ctx_), true);

    const llama_vocab* vocab = llama_model_get_vocab(model_);

    // ── 1. BGRA 像素 → RGB 数据（mtmd_bitmap 需要 RGBRGBRGB 格式） ──
    std::vector<uint8_t> rgb(static_cast<size_t>(width) * height * 3);
    for (int i = 0; i < width * height; i++) {
        rgb[i * 3 + 0] = bgra_pixels[i * 4 + 2]; // R (from BGRA offset 2)
        rgb[i * 3 + 1] = bgra_pixels[i * 4 + 1]; // G
        rgb[i * 3 + 2] = bgra_pixels[i * 4 + 0]; // B (from BGRA offset 0)
    }

    // ── 2. 创建 mtmd bitmap（直接 RGB 数据，无需 BMP 编码） ──
    mtmd_bitmap* bitmap = mtmd_bitmap_init(
        static_cast<uint32_t>(width),
        static_cast<uint32_t>(height),
        rgb.data());
    if (!bitmap) {
        return makeFallbackJson("mtmd_bitmap_init 失败");
    }

    // ── 3. 构建包含图像标记的提示词 ──
    const char* marker = mtmd_default_marker();
    std::string prompt_text;
    prompt_text += "<|im_start|>system\n";
    prompt_text += SYSTEM_PROMPT;
    prompt_text += "<|im_end|>\n<|im_start|>user\n";
    prompt_text += marker;  // 图像标记，mtmd 会替换为图像 token
    prompt_text += "\n";
    if (!user_prompt.empty()) {
        prompt_text += user_prompt + "\n";
    }
    prompt_text += "Analyze the game screenshot and respond with JSON:";
    prompt_text += "<|im_end|>\n<|im_start|>assistant\n";

    // ── 4. 使用 mtmd 对文本 + 图像进行统一 tokenize ──
    mtmd_input_text input_text;
    input_text.text          = prompt_text.c_str();
    input_text.add_special   = true;
    input_text.parse_special = true;

    mtmd_input_chunks* chunks = mtmd_input_chunks_init();
    const mtmd_bitmap* bitmap_ptr = bitmap;
    int32_t tok_res = mtmd_tokenize(mtmdCtx_, chunks, &input_text, &bitmap_ptr, 1);

    mtmd_bitmap_free(bitmap);  // tokenize 后不再需要原始 bitmap

    if (tok_res != 0) {
        mtmd_input_chunks_free(chunks);
        return makeFallbackJson("mtmd_tokenize 失败, code=" + std::to_string(tok_res));
    }

    // ── 5. 一次性 eval 所有 chunks（文本 + 图像 embedding） ──
    llama_pos n_past = 0;
    llama_pos new_n_past = 0;
    int32_t eval_res = mtmd_helper_eval_chunks(
        mtmdCtx_, ctx_, chunks,
        n_past,              // 起始位置
        0,                   // seq_id
        config_.n_batch,     // n_batch
        true,                // logits_last
        &new_n_past);

    mtmd_input_chunks_free(chunks);

    if (eval_res != 0) {
        return makeFallbackJson("mtmd_helper_eval_chunks 失败, code=" + std::to_string(eval_res));
    }
    n_past = new_n_past;

    // ── 6. 自回归生成循环 ──
    std::string output;
    output.reserve(config_.max_tokens * 4);

    // 分配可复用的单 token batch
    llama_batch batch = llama_batch_init(1, 0, 1);

    for (int i = 0; i < config_.max_tokens; i++) {
        // 使用采样链从 logits 中采样
        llama_token new_token = llama_sampler_sample(sampler_, ctx_, -1);

        // 检查是否为终止 token
        if (llama_vocab_is_eog(vocab, new_token)) {
            break;
        }

        // 将 token ID 转为文本片段
        char buf[256];
        int n = llama_token_to_piece(vocab, new_token, buf, sizeof(buf), 0, true);
        if (n > 0) {
            output.append(buf, n);
        }

        // 准备单 token batch 送回模型
        batch.n_tokens       = 1;
        batch.token[0]       = new_token;
        batch.pos[0]         = n_past;
        batch.n_seq_id[0]    = 1;
        batch.seq_id[0][0]   = 0;
        batch.logits[0]      = 1;  // 需要这个位置的 logits

        if (llama_decode(ctx_, batch) != 0) {
            setError("llama_decode 失败（生成阶段）");
            break;
        }
        n_past++;

        // 提前终止：检测到完整的 JSON 对象闭合
        if (!output.empty() && output.back() == '}') {
            int depth = 0;
            for (char c : output) {
                if (c == '{') depth++;
                else if (c == '}') depth--;
            }
            if (depth == 0) break;
        }
    }

    llama_batch_free(batch);

    if (output.empty()) {
        return makeFallbackJson("模型未生成任何输出");
    }

    return output;
}

// ============================================================
//  engine_name / last_error / setError / makeFallbackJson
// ============================================================

std::string VlmEngine::engine_name() const {
    if (loaded_.load()) {
        return "VLM_Engine (llama.cpp)";
    }
    return "VLM_Engine (not loaded)";
}

std::string VlmEngine::last_error() const {
    std::lock_guard<std::mutex> lock(errorMutex_);
    return lastError_;
}

void VlmEngine::setError(const std::string& msg) {
    std::lock_guard<std::mutex> lock(errorMutex_);
    lastError_ = msg;
}

std::string VlmEngine::makeFallbackJson(const std::string& reason) {
    // 返回安全的 fallback JSON，确保 parse_ai_response 能正确处理
    return R"({"current_status":"unknown","detected_units":[],"tactical_advice":")" +
           reason + R"(","confidence":0.0})";
}

} // namespace navi
