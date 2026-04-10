#pragma once

#include "inference_engine.h"

#include <string>
#include <vector>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <queue>
#include <atomic>

// ── llama.cpp / mtmd 前向声明（避免在头文件暴露底层实现细节） ──
struct llama_model;
struct llama_context;
struct llama_sampler;
struct mtmd_context;

namespace navi {

// ============================================================
//  VLM_Engine 配置参数
// ============================================================
struct VlmConfig {
    std::string model_path;       // 主模型权重路径 (.gguf)
    std::string mmproj_path;      // 视觉投影器权重路径 (mmproj.gguf)
    int   n_gpu_layers = 99;      // GPU 卸载层数（99 = 尽可能全部卸载到 VRAM）
    int   n_threads    = 4;       // CPU 推理线程数（建议设为物理核心数的一半）
    int   n_ctx        = 4096;    // 上下文窗口大小（需容纳图像 token + 文本 token + 生成）
    int   n_batch      = 512;     // 批处理大小（影响图像 embedding 分批送入的粒度）
    float temperature  = 0.1f;    // 采样温度（低温 = 更确定性的输出，适合结构化 JSON）
    int   max_tokens   = 512;     // 最大生成 token 数

    // ── 来自 GameProfile 的提示词（为空时使用内置默认值） ──
    std::string system_prompt;    // 系统提示词
    std::string user_prompt;      // 用户提示词
};

// ============================================================
//  VLM_Engine — 基于 llama.cpp 的多模态视觉语言推理引擎
//
//  职责：
//  1. 加载主 LLM 权重 + CLIP 视觉投影器权重
//  2. 将 BGRA 像素数据转换为 RGB → CLIP 编码为图像 embedding
//  3. 拼接系统提示 + 图像 embedding + 用户提示 → llama_decode
//  4. 自回归生成 JSON 格式的分析结果
//
//  线程安全：
//  - analyze_frame() 可从任意线程调用
//  - 内部持有互斥锁，同一时刻只有一次推理在执行
//  - 主 UI 线程通过 AppGui 的异步管道调用本引擎
// ============================================================
class VlmEngine : public IInferenceEngine {
public:
    explicit VlmEngine(const VlmConfig& config);
    ~VlmEngine() override;

    // 禁止拷贝
    VlmEngine(const VlmEngine&)            = delete;
    VlmEngine& operator=(const VlmEngine&) = delete;

    // ── IInferenceEngine 接口实现 ──

    /// 分析游戏画面帧
    /// @param image_data  BGRA 像素数据（与 FrameData::pixels 一致）
    /// @param width/height 画面尺寸
    /// @param voice_text_prompt 可选的用户语音输入文本
    /// @return 符合 GameStateData 格式的 JSON 字符串
    std::string analyze_frame(
        const std::vector<uint8_t>& image_data,
        int width, int height,
        const std::string& voice_text_prompt) override;

    std::string engine_name() const override;

    /// 模型是否已成功加载
    bool is_loaded() const { return loaded_.load(); }

    /// 获取加载/推理过程中的错误信息
    std::string last_error() const;

private:
    // ── 初始化与清理 ──
    bool loadModels();
    void unloadModels();

    // ── 推理核心 ──
    std::string doInference(
        const std::vector<uint8_t>& bgra_pixels,
        int width, int height,
        const std::string& user_prompt);

    // ── 辅助：生成安全的 Fallback JSON ──
    static std::string makeFallbackJson(const std::string& reason);

    // ── 配置 ──
    VlmConfig config_;

    // ── llama.cpp 核心对象 ──
    llama_model*    model_    = nullptr;   // 主 LLM 模型
    llama_context*  ctx_      = nullptr;   // LLM 推理上下文
    mtmd_context*   mtmdCtx_  = nullptr;   // 多模态视觉编码器（替代旧版 CLIP）
    llama_sampler*  sampler_  = nullptr;   // 采样链（temp + top_k + top_p + dist）

    // ── 线程安全 ──
    std::mutex     inferenceMutex_;  // 保证同一时刻只有一次推理
    std::atomic<bool> loaded_{ false };

    // ── 错误信息 ──
    mutable std::mutex errorMutex_;
    std::string lastError_;
    void setError(const std::string& msg);
};

} // namespace navi
