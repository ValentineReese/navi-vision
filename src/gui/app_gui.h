#pragma once

#include <memory>
#include <vector>
#include <string>
#include <chrono>
#include <thread>
#include <atomic>
#include <mutex>

#include <d3d11.h>
#include <wrl/client.h>
#include <imgui.h>

#include "../core/frame_buffer.h"
#include "../core/game_state.h"
#include "../core/response_parser.h"
#include "../capture/wgc_capture.h"
#include "../capture/window_enumerator.h"
#include "../inference/inference_engine.h"

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
    AppGui(std::shared_ptr<WgcCapture>       capture,
           std::shared_ptr<FrameBuffer>    buffer,
           std::shared_ptr<IInferenceEngine> engine,
           ID3D11Device*                  device,
           ID3D11DeviceContext*           context);
    ~AppGui();

    /// 每帧调用，渲染整个 GUI
    void render();

private:
    // ── GUI 子模块 ──
    void renderControlPanel();
    void renderWindowSelector();
    void renderPreview();
    void renderAIPanel();
    void renderStatusBar();

    // ── 眼睛/话筒的自定义图标绘制 ──
    void drawEyeButton(const char* label, bool isOpen, ImVec2 size);
    void drawMicButton(const char* label, bool isOn,   ImVec2 size);

    // ── 预览纹理管理 ──
    void updatePreviewTexture();

    // ── 工具函数 ──
    static std::string wstringToUtf8(const std::wstring& wstr);

    // ── 核心模块引用 ──
    std::shared_ptr<WgcCapture>       capture_;
    std::shared_ptr<FrameBuffer>    buffer_;
    std::shared_ptr<IInferenceEngine> engine_;
    ID3D11Device*                  device_;
    ID3D11DeviceContext*           context_;

    // ── 界面状态 ──
    bool visionActive_       = false;
    bool micActive_          = false;
    bool showWindowSelector_ = false;

    // ── 窗口列表 ──
    std::vector<WindowInfo> windowList_;

    // ── 预览纹理 ──
    Microsoft::WRL::ComPtr<ID3D11Texture2D>          previewTexture_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> previewSRV_;
    int previewWidth_  = 0;
    int previewHeight_ = 0;
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
};

} // namespace navi
