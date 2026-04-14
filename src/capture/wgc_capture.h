#pragma once

// ── Windows 宏定义（必须在所有 Windows 头文件之前） ──
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

// ── C++/WinRT 头文件 ──
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>

// ── WinRT / D3D11 互操作头文件 ──
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>

#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <string>

#include "capture_engine.h"
#include "../core/frame_buffer.h"

namespace navi {

/// Windows Graphics Capture (WGC) 引擎
///
/// 使用独立的 D3D11 设备在后台线程中运行，
/// 通过 WGC API 捕获指定窗口的画面，并以限频方式
/// （默认 3 FPS）将 BGR 像素数据写入线程安全的 FrameBuffer。
class WgcCapture : public ICaptureEngine {
public:
    explicit WgcCapture(std::shared_ptr<FrameBuffer> buffer);
    ~WgcCapture() override;

    // 禁止拷贝
    WgcCapture(const WgcCapture&)            = delete;
    WgcCapture& operator=(const WgcCapture&) = delete;

    // ── ICaptureEngine 接口 ──
    bool start(NativeWindowHandle windowHandle, float targetFps = 3.0f) override;
    void stop() override;
    bool isCapturing() const override { return capturing_.load(); }
    std::string lastError() const override;

private:
    // ── 内部方法 ──
    void captureLoop();
    bool initD3D();
    bool initCapture(HWND hwnd);
    void cleanup();

    // ── 共享帧缓冲区 ──
    std::shared_ptr<FrameBuffer> buffer_;

    // ── D3D11 设备（捕获专用，独立于 GUI 的渲染设备） ──
    Microsoft::WRL::ComPtr<ID3D11Device>        d3dDevice_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> d3dContext_;

    // ── WGC 资源 ──
    winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice winrtDevice_{ nullptr };
    winrt::Windows::Graphics::Capture::GraphicsCaptureItem         captureItem_{ nullptr };
    winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool  framePool_{ nullptr };
    winrt::Windows::Graphics::Capture::GraphicsCaptureSession      captureSession_{ nullptr };

    // ── 线程控制 ──
    std::thread       captureThread_;
    std::atomic<bool> capturing_{ false };
    std::atomic<bool> stopRequested_{ false };

    // ── 初始化同步（主线程等待捕获线程初始化完成） ──
    std::mutex              initMutex_;
    std::condition_variable initCv_;
    std::atomic<bool>       initDone_{ false };
    std::atomic<bool>       initSuccess_{ false };

    // ── 限频参数 ──
    float targetFps_ = 3.0f;
    std::chrono::steady_clock::time_point lastCaptureTime_;

    // ── 暂存纹理缓存（避免每帧重建） ──
    Microsoft::WRL::ComPtr<ID3D11Texture2D> stagingTexture_;
    int stagingWidth_  = 0;
    int stagingHeight_ = 0;

    // ── 目标窗口 ──
    HWND targetHwnd_ = nullptr;

    // ── 错误信息 ──
    mutable std::mutex errorMutex_;
    std::string        lastError_;

    void setError(const std::string& msg);
};

} // namespace navi
