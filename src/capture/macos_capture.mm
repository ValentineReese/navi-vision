#include "macos_capture.h"

#include <chrono>
#include <vector>
#include <CoreGraphics/CoreGraphics.h>

namespace navi {

MacosCapture::MacosCapture(std::shared_ptr<FrameBuffer> buffer)
    : buffer_(std::move(buffer)) {
}

MacosCapture::~MacosCapture() {
    stop();
}

bool MacosCapture::start(NativeWindowHandle windowHandle, float targetFps) {
    if (capturing_.load())
        return false;

    targetWindowId_ = static_cast<CGWindowID>(windowHandle);
    targetFps_      = targetFps;
    stopRequested_  = false;

    captureThread_ = std::thread(&MacosCapture::captureLoop, this);
    capturing_ = true;
    return true;
}

void MacosCapture::stop() {
    stopRequested_ = true;
    if (captureThread_.joinable())
        captureThread_.join();
    capturing_ = false;
}

std::string MacosCapture::lastError() const {
    std::lock_guard<std::mutex> lock(errorMutex_);
    return lastError_;
}

void MacosCapture::setError(const std::string& msg) {
    std::lock_guard<std::mutex> lock(errorMutex_);
    lastError_ = msg;
}

void MacosCapture::captureLoop() {
    auto frameInterval = std::chrono::milliseconds(
        static_cast<int>(1000.0f / targetFps_));
    auto lastCaptureTime = std::chrono::steady_clock::now();

    while (!stopRequested_.load()) {
        auto now     = std::chrono::steady_clock::now();
        auto elapsed = now - lastCaptureTime;

        if (elapsed < frameInterval) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }

        // 使用 CGWindowListCreateImage 截取指定窗口
        // kCGWindowListOptionIncludingWindow — 仅包含目标窗口
        // kCGWindowImageBoundsIgnoreFraming — 仅窗口内容，不含标题栏外框
        CGImageRef image = CGWindowListCreateImage(
            CGRectNull,  // 自动使用窗口的bounds
            kCGWindowListOptionIncludingWindow,
            targetWindowId_,
            kCGWindowImageBoundsIgnoreFraming | kCGWindowImageNominalResolution);

        if (!image) {
            setError("CGWindowListCreateImage returned null");
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        size_t width       = CGImageGetWidth(image);
        size_t height      = CGImageGetHeight(image);
        size_t bitsPerComp = CGImageGetBitsPerComponent(image);
        size_t bytesPerRow = CGImageGetBytesPerRow(image);

        // 创建 BGRA 位图上下文以统一像素格式
        CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
        size_t bgraRowBytes = width * 4;
        std::vector<uint8_t> bgraPixels(bgraRowBytes * height);

        CGContextRef ctx = CGBitmapContextCreate(
            bgraPixels.data(),
            width, height,
            8,                          // bits per component
            bgraRowBytes,
            colorSpace,
            kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little  // BGRA
        );

        if (ctx) {
            CGContextDrawImage(ctx, CGRectMake(0, 0, width, height), image);
            CGContextRelease(ctx);

            // 写入 FrameBuffer
            auto frame = std::make_shared<FrameData>();
            frame->pixels   = std::move(bgraPixels);
            frame->width    = static_cast<int>(width);
            frame->height   = static_cast<int>(height);
            frame->channels = 4;
            frame->timestamp = std::chrono::steady_clock::now();
            buffer_->write(std::move(frame));
        }

        CGColorSpaceRelease(colorSpace);
        CGImageRelease(image);

        lastCaptureTime = std::chrono::steady_clock::now();
    }
}

} // namespace navi
