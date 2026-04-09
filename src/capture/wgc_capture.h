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

#include "../core/frame_buffer.h"

namespace navi {

/// Windows Graphics Capture (WGC) 引擎
///
/// 使用独立的 D3D11 设备在后台线程中运行，
/// 通过 WGC API 捕获指定窗口的画面，并以限频方式
/// （默认 3 FPS）将 BGR 像素数据写入线程安全的 FrameBuffer。
class WgcCapture {
public:
    explicit WgcCapture(std::shared_ptr<FrameBuffer> buffer);
    ~WgcCapture();

    // 禁止拷贝
    WgcCapture(const WgcCapture&)            = delete;
    WgcCapture& operator=(const WgcCapture&) = delete;

    /// 启动对指定窗口的捕获（阻塞直到初始化完成）
    /// @param targetHwnd 目标窗口句柄
    /// @param targetFps  目标帧率（建议 2~5 FPS）
    /// @return 初始化是否成功
    bool start(HWND targetHwnd, float targetFps = 3.0f);

    /// 停止捕获并释放资源
    void stop();

    /// 是否正在捕获
    bool isCapturing() const { return capturing_.load(); }

    /// 获取最近一次的错误信息
    std::string lastError() const;

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

    // ── 目标窗口 ──
    HWND targetHwnd_ = nullptr;

    // ── 错误信息 ──
    mutable std::mutex errorMutex_;
    std::string        lastError_;

    void setError(const std::string& msg);
};

} // namespace navi
