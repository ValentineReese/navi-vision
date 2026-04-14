#pragma once

#include <memory>
#include <vector>
#include <string>
#include <chrono>
#include <thread>
#include <atomic>
#include <mutex>

#ifdef _WIN32
#include <d3d11.h>
#include <wrl/client.h>
#endif
#include <imgui.h>

#include "../core/frame_buffer.h"
#include "../core/game_state.h"
#include "../core/game_profile.h"
#include "../core/log_buffer.h"
#include "../core/response_parser.h"
#include "../capture/capture_engine.h"
#include "../capture/window_enumerator.h"
#include "../inference/inference_engine.h"
#include "../models/model_manager.h"
#include "preview_texture.h"

namespace navi {

/// ImGui 应用界面管理器
///
/// 负责渲染 NaviVision 的控制面板，包括：
/// - "眼睛"按钮（开启/关闭视觉捕获）
/// - "话筒"按钮（占位，暂不实现语音逻辑）
/// - 窗口选择弹窗
/// - 捕获画面实时预览
/// - 状态信息显示
class AppGui {
public:
#ifdef _WIN32
    AppGui(std::shared_ptr<ICaptureEngine>    capture,
           std::shared_ptr<FrameBuffer>       buffer,
           std::shared_ptr<IInferenceEngine>  engine,
           ID3D11Device*                      device,
           ID3D11DeviceContext*                context);
#else
    AppGui(std::shared_ptr<ICaptureEngine>    capture,
           std::shared_ptr<FrameBuffer>       buffer,
           std::shared_ptr<IInferenceEngine>  engine);
#endif
    ~AppGui();

    /// 每帧调用，渲染整个 GUI
    void render();

private:
    // ── GUI 子模块 ──
    void renderControlPanel();
    void renderWindowSelector();
    void renderPreview();
    void renderAIPanel();
    void renderLogWindow();
    void renderStatusBar();
    void renderModelSettings();
    void startModelDownload();
    void startModelLoad();

    // ── 眼睛/话筒的自定义图标绘制 ──
    void drawEyeButton(const char* label, bool isOpen, ImVec2 size);
    void drawMicButton(const char* label, bool isOn,   ImVec2 size);

    // ── 预览纹理管理 ──
    void updatePreviewTexture();

    // ── 核心模块引用 ──
    std::shared_ptr<ICaptureEngine>    capture_;
    std::shared_ptr<FrameBuffer>       buffer_;
    std::shared_ptr<IInferenceEngine>  engine_;

    // ── 预览纹理（跨平台抽象） ──
    std::unique_ptr<PreviewTexture> previewTex_;

    // ── 界面状态 ──
    bool visionActive_       = false;
    bool micActive_          = false;
    bool showWindowSelector_ = false;

    // ── 窗口列表 ──
    std::vector<WindowInfo> windowList_;

    // ── 预览时间戳 ──
    std::chrono::steady_clock::time_point lastPreviewTimestamp_;

    // ── 每帧缓存的 FrameBuffer 读取结果 ──
    std::shared_ptr<FrameData> cachedFrame_;

    // ── AI analysis state ──
    GameStateData lastAIResult_;
    std::chrono::steady_clock::time_point lastAnalysisTime_;
    float analysisIntervalSec_ = 2.0f;  // Run mock inference every N seconds

    // ── Async inference ──
    std::thread inferenceThread_;
    std::mutex  inferenceMutex_;
    GameStateData pendingAIResult_;
    std::atomic<bool> inferenceRunning_{ false };
    std::atomic<bool> hasNewResult_{ false };

    void runInferenceAsync(std::vector<uint8_t> pixels, int width, int height);

    // ── Model Settings UI ──
    bool showModelSettings_ = false;
    std::vector<ModelEntry> defaultModels_;
    int selectedModelIdx_ = 0;
    int selectedDevice_ = 0;         // 0 = GPU, 1 = CPU
    char customModelUrl_[1024] = {};
    char customMmprojUrl_[1024] = {};
    char proxyAddr_[256] = "127.0.0.1:7890";
    std::string modelsDir_;

    // ── Download state ──
    std::thread downloadThread_;
    std::atomic<bool> downloading_{false};
    std::atomic<bool> cancelDownload_{false};
    std::atomic<float> downloadProgress_{0.0f};
    std::atomic<size_t> dlBytesDown_{0};
    std::atomic<size_t> dlBytesTotal_{0};
    std::string downloadStatus_;
    std::mutex downloadMsgMutex_;

    // ── Model loading state ──
    std::thread modelLoadThread_;
    std::atomic<bool> modelLoading_{false};
    std::atomic<bool> modelLoadDone_{false};
    std::shared_ptr<IInferenceEngine> pendingEngine_;
    std::mutex modelLoadMutex_;
    std::string modelLoadError_;

    // ── Game Profile ──
    ProfileManager profileManager_;
    int selectedProfileIdx_ = 0;
    void applyProfile(int index);

    // ── Log Window ──
    bool showLogWindow_ = false;
};

} // namespace navi
