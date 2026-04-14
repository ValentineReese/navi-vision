#pragma once

// ============================================================
//  MacosCapture — macOS 屏幕捕获引擎
//
//  使用 CoreGraphics CGWindowListCreateImage 实现窗口捕获。
//  在后台线程中以目标帧率轮询截图，将 BGRA 像素数据
//  写入共享 FrameBuffer。
//
//  注：ScreenCaptureKit（macOS 12.3+）可提供更高效的捕获，
//  但 CGWindowList 兼容性更好且实现更简单。
// ============================================================

#include "capture_engine.h"

#include <thread>
#include <atomic>
#include <mutex>
#include <string>
#include <memory>
#include <CoreGraphics/CoreGraphics.h>

#include "../core/frame_buffer.h"

namespace navi {

class MacosCapture : public ICaptureEngine {
public:
    explicit MacosCapture(std::shared_ptr<FrameBuffer> buffer);
    ~MacosCapture() override;

    // 禁止拷贝
    MacosCapture(const MacosCapture&)            = delete;
    MacosCapture& operator=(const MacosCapture&) = delete;

    bool start(NativeWindowHandle windowHandle, float targetFps = 3.0f) override;
    void stop() override;
    bool isCapturing() const override { return capturing_.load(); }
    std::string lastError() const override;

private:
    void captureLoop();
    void setError(const std::string& msg);

    std::shared_ptr<FrameBuffer> buffer_;

    CGWindowID targetWindowId_ = 0;
    float targetFps_ = 3.0f;

    std::thread      captureThread_;
    std::atomic<bool> capturing_{false};
    std::atomic<bool> stopRequested_{false};

    mutable std::mutex errorMutex_;
    std::string lastError_;
};

} // namespace navi
