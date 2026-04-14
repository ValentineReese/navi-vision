#pragma once

// ============================================================
//  ICaptureEngine — 跨平台屏幕捕获抽象接口
//
//  Windows: WgcCapture 实现（WGC API）
//  macOS:   MacosCapture 实现（ScreenCaptureKit / CGWindowList）
// ============================================================

#include <memory>
#include <string>
#include <atomic>
#include "../core/frame_buffer.h"

namespace navi {

/// 平台无关的窗口句柄类型
/// Windows: HWND (void*), macOS: CGWindowID (uint32_t packed into uintptr_t)
using NativeWindowHandle = uintptr_t;

constexpr NativeWindowHandle kInvalidWindowHandle = 0;

/// 屏幕捕获引擎抽象接口
class ICaptureEngine {
public:
    virtual ~ICaptureEngine() = default;

    /// 启动捕获指定窗口
    /// @param windowHandle 目标窗口的原生句柄
    /// @param targetFps    目标帧率
    /// @return 是否成功
    virtual bool start(NativeWindowHandle windowHandle, float targetFps = 3.0f) = 0;

    /// 停止捕获
    virtual void stop() = 0;

    /// 是否正在捕获
    virtual bool isCapturing() const = 0;

    /// 获取最近一次错误信息
    virtual std::string lastError() const = 0;
};

} // namespace navi
