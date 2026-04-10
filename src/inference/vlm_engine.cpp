#include "vlm_engine.h"

// ── llama.cpp C API 头文件 ──
#include <llama.h>
#include <clip.h>
#include <llava.h>

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
    // llama_model_params 控制模型加载行为：
    //   n_gpu_layers: 将多少层卸载到 GPU（0 = 纯 CPU，99 = 尽量全部上 GPU）
    //   use_mmap: 内存映射加载，减少物理内存占用
    llama_model_params model_params = llama_model_default_params();
    model_params.n_gpu_layers = config_.n_gpu_layers;
    model_params.use_mmap     = true;

    model_ = llama_load_model_from_file(config_.model_path.c_str(), model_params);
    if (!model_) {
        setError("llama_load_model_from_file 失败，可能原因：文件损坏、VRAM 不足、格式不兼容");
        return false;
    }

    // ── 2. 创建推理上下文 ──
    // llama_context_params 控制运行时行为：
    //   n_ctx:     上下文窗口，需足够容纳图像 token（通常 576~2048）+ 文本 + 生成
    //   n_threads: CPU 推理线程数，建议 = 物理核心数 / 2（避免与游戏争抢）
    //   n_batch:   每次 decode 最多处理的 token 数
    llama_context_params ctx_params = llama_context_default_params();
    ctx_params.n_ctx     = config_.n_ctx;
    ctx_params.n_threads = config_.n_threads;
    ctx_params.n_batch   = config_.n_batch;
    ctx_params.seed      = 42;

    ctx_ = llama_new_context_with_model(model_, ctx_params);
    if (!ctx_) {
        setError("llama_new_context_with_model 失败，可能 n_ctx 过大导致内存分配失败");
        return false;
    }

    // ── 3. 加载 CLIP 视觉投影器（mmproj.gguf） ──
    // clip_model_load 创建一个独立的视觉编码器上下文
    // verbosity=1 打印加载信息便于调试
    clipCtx_ = clip_model_load(config_.mmproj_path.c_str(), /*verbosity=*/ 1);
    if (!clipCtx_) {
        setError("clip_model_load 失败，视觉投影器文件可能损坏");
        return false;
    }

    // ── 4. 校验 CLIP embedding 维度与 LLM embedding 维度是否匹配 ──
    if (!llava_validate_embed_size(ctx_, clipCtx_)) {
        setError("CLIP 与 LLM 的 embedding 维度不匹配，请确认 mmproj 与主模型版本对应");
        return false;
    }

    loaded_.store(true);
    return true;
}

// ============================================================
//  资源释放
// ============================================================
void VlmEngine::unloadModels() {
    loaded_.store(false);

    if (clipCtx_) {
        clip_free(clipCtx_);
        clipCtx_ = nullptr;
    }
    if (ctx_) {
        llama_free(ctx_);
        ctx_ = nullptr;
    }
    if (model_) {
        llama_free_model(model_);
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
    llama_kv_cache_clear(ctx_);
    int n_past = 0;  // 跟踪当前已写入 KV 缓存的 token 位置

    // ── 1. BGRA 像素 → 内存 BMP 编码 ──
    // clip_image_u8 是不透明类型，不能直接赋值字段。
    // 因此我们把原始像素编码为 24-bit BMP（无压缩），
    // 再通过 llava_image_embed_make_with_bytes 让 llava 内部解码。
    const int row_bytes   = width * 3;
    const int row_padding = (4 - (row_bytes % 4)) % 4;
    const int stride      = row_bytes + row_padding;
    const int pixel_size  = stride * height;
    const int file_size   = 54 + pixel_size;  // 14 (file hdr) + 40 (info hdr) + pixels

    std::vector<uint8_t> bmp(file_size, 0);

    // ── BMP File Header (14 bytes) ──
    bmp[0] = 'B'; bmp[1] = 'M';
    std::memcpy(&bmp[2],  &file_size, 4);   // bfSize
    int data_offset = 54;
    std::memcpy(&bmp[10], &data_offset, 4);  // bfOffBits

    // ── BMP Info Header (BITMAPINFOHEADER, 40 bytes) ──
    int info_size = 40;
    std::memcpy(&bmp[14], &info_size, 4);    // biSize
    std::memcpy(&bmp[18], &width, 4);        // biWidth
    std::memcpy(&bmp[22], &height, 4);       // biHeight (positive = bottom-up)
    short planes = 1, bpp = 24;
    std::memcpy(&bmp[26], &planes, 2);       // biPlanes
    std::memcpy(&bmp[28], &bpp, 2);          // biBitCount
    // biCompression = 0 (BI_RGB), rest zero → already zero-initialized

    // ── 像素数据（BMP 行序 = 底→顶，像素序 = BGR） ──
    // BGRA 源数据刚好是 B-G-R 顺序，只需跳过 A 通道
    for (int y = 0; y < height; y++) {
        const int src_row = (height - 1 - y);  // BMP 底→顶翻转
        const uint8_t* src = bgra_pixels.data() + src_row * width * 4;
        uint8_t* dst = bmp.data() + 54 + y * stride;
        for (int x = 0; x < width; x++) {
            dst[x * 3 + 0] = src[x * 4 + 0]; // B
            dst[x * 3 + 1] = src[x * 4 + 1]; // G
            dst[x * 3 + 2] = src[x * 4 + 2]; // R
        }
        // padding bytes are already zero
    }

    // ── 2. 使用 llava API 编码图像 → embedding ──
    // llava_image_embed_make_with_bytes 内部调用 clip_image_load_from_bytes
    // 解码 BMP，然后预处理 + ViT 前向传播，返回完整的 embedding
    llava_image_embed* embed = llava_image_embed_make_with_bytes(
        clipCtx_, config_.n_threads, bmp.data(), static_cast<int>(bmp.size()));

    if (!embed) {
        return makeFallbackJson("图像编码失败");
    }

    // ── 3. 拼接多模态上下文：系统提示 + 图像 embedding + 用户提示 ──

    // 3a. Eval 系统提示（文本 token 写入 KV 缓存）
    if (!evalText(SYSTEM_PROMPT, n_past, /*add_bos=*/ true)) {
        llava_image_embed_free(embed);
        return makeFallbackJson("系统提示 tokenize/eval 失败");
    }

    // 3b. Eval 图像 embedding（直接将 float embedding 写入 KV 缓存）
    // llava_eval_image_embed 内部按 n_batch 分批调用 llama_decode
    // 使用 batch.embd（而非 batch.token）直接传入浮点 embedding
    if (!llava_eval_image_embed(ctx_, embed, config_.n_batch, &n_past)) {
        llava_image_embed_free(embed);
        return makeFallbackJson("图像 embedding eval 失败");
    }
    llava_image_embed_free(embed);  // embedding 已写入 KV 缓存，释放

    // 3c. Eval 用户提示（图像后的文本 token）
    std::string final_prompt = "\n";
    if (!user_prompt.empty()) {
        final_prompt += user_prompt + "\n";
    }
    final_prompt += "Analyze the game screenshot and respond with JSON:\n";

    if (!evalText(final_prompt, n_past, /*add_bos=*/ false)) {
        return makeFallbackJson("用户提示 eval 失败");
    }

    // ── 4. 自回归生成循环 ──
    // 每次从 logits 中采样一个 token，追加到输出，直到遇到 EOS 或达到上限
    std::string output;
    output.reserve(config_.max_tokens * 4);  // 预分配，减少 realloc

    const int n_vocab = llama_n_vocab(model_);

    for (int i = 0; i < config_.max_tokens; i++) {
        // 获取最后一个位置的 logits（shape: [n_vocab]）
        float* logits = llama_get_logits_ith(ctx_, -1);
        if (!logits) break;

        // ── 构建候选 token 数组 ──
        std::vector<llama_token_data> candidates(n_vocab);
        for (int j = 0; j < n_vocab; j++) {
            candidates[j] = llama_token_data{ j, logits[j], 0.0f };
        }
        llama_token_data_array candidates_p = {
            candidates.data(),
            static_cast<size_t>(n_vocab),
            false  // 未排序
        };

        // ── 采样策略：低温 + top-k + top-p ──
        // 低温度（0.1）确保输出高度确定，适合结构化 JSON
        llama_sample_temp(ctx_, &candidates_p, config_.temperature);
        llama_sample_top_k(ctx_, &candidates_p, 40, 1);
        llama_sample_top_p(ctx_, &candidates_p, 0.95f, 1);

        // 从过滤后的候选中采样
        llama_token new_token = llama_sample_token(ctx_, &candidates_p);

        // 检查是否为终止 token（EOS 或其他 end-of-generation 标记）
        if (llama_token_is_eog(model_, new_token)) {
            break;
        }

        // ── 将 token ID 转为文本片段 ──
        char buf[256];
        int n = llama_token_to_piece(model_, new_token, buf, sizeof(buf), 0, true);
        if (n > 0) {
            output.append(buf, n);
        }

        // ── 将生成的 token 送回模型进行下一步 decode ──
        // 使用 llama_batch_get_one 创建单 token 的 batch
        llama_batch batch = llama_batch_get_one(&new_token, 1, n_past, 0);
        if (llama_decode(ctx_, batch) != 0) {
            setError("llama_decode 失败（生成阶段）");
            break;
        }
        n_past++;

        // ── 提前终止：检测到完整的 JSON 对象闭合 ──
        // 当检测到 "}" 时，可以提前停止生成，节省推理时间
        if (!output.empty() && output.back() == '}') {
            // 验证括号平衡
            int depth = 0;
            for (char c : output) {
                if (c == '{') depth++;
                else if (c == '}') depth--;
            }
            if (depth == 0) break;  // JSON 对象完整闭合
        }
    }

    // 如果输出为空，返回 fallback
    if (output.empty()) {
        return makeFallbackJson("模型未生成任何输出");
    }

    return output;
}

// ============================================================
//  辅助：Tokenize + Eval 一段文本
//
//  将文本字符串转为 token 序列，分批送入 llama_decode
//  写入 KV 缓存后 n_past 相应递增
// ============================================================
bool VlmEngine::evalText(const std::string& text, int& n_past, bool add_bos) {
    // ── Tokenize ──
    // 先以 n_tokens_max=0 调用获取所需的 token 数量（返回负值 = 实际数量的相反数）
    int n_tokens = llama_tokenize(
        model_, text.c_str(), static_cast<int>(text.size()),
        nullptr, 0, add_bos, /*parse_special=*/ true);

    // llama_tokenize 返回负数表示需要的 buffer 大小
    if (n_tokens < 0) n_tokens = -n_tokens;

    std::vector<llama_token> tokens(n_tokens);
    int actual = llama_tokenize(
        model_, text.c_str(), static_cast<int>(text.size()),
        tokens.data(), n_tokens, add_bos, /*parse_special=*/ true);

    if (actual < 0) {
        setError("llama_tokenize 失败: text_len=" + std::to_string(text.size()));
        return false;
    }
    tokens.resize(actual);

    // ── 分批 Eval ──
    // 按 n_batch 大小分批调用 llama_decode，将 token 写入 KV 缓存
    for (int i = 0; i < actual; i += config_.n_batch) {
        int n_eval = std::min(config_.n_batch, actual - i);

        llama_batch batch = llama_batch_get_one(
            tokens.data() + i, n_eval, n_past, /*seq_id=*/ 0);

        if (llama_decode(ctx_, batch) != 0) {
            setError("llama_decode 失败: n_past=" + std::to_string(n_past));
            return false;
        }
        n_past += n_eval;
    }

    return true;
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
