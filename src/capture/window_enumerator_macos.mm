#include "window_enumerator.h"
#include <CoreGraphics/CoreGraphics.h>
#include <CoreFoundation/CoreFoundation.h>

namespace navi {

// ── CoreFoundation 字符串 → std::string 辅助函数 ──
static std::string cfStringToStdString(CFStringRef cfStr) {
    if (!cfStr) return {};

    CFIndex length = CFStringGetLength(cfStr);
    CFIndex maxSize = CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8) + 1;
    std::string result(maxSize, '\0');

    if (CFStringGetCString(cfStr, result.data(), maxSize, kCFStringEncodingUTF8)) {
        result.resize(std::strlen(result.c_str()));
        return result;
    }
    return {};
}

bool WindowEnumerator::hasScreenCapturePermission() {
    return CGPreflightScreenCaptureAccess();
}

void WindowEnumerator::requestScreenCapturePermission() {
    CGRequestScreenCaptureAccess();
}

std::vector<WindowInfo> WindowEnumerator::enumerate() {
    std::vector<WindowInfo> windows;

    // 获取所有屏幕上的窗口列表
    CFArrayRef windowList = CGWindowListCopyWindowInfo(
        kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements,
        kCGNullWindowID);

    if (!windowList)
        return windows;

    CFIndex count = CFArrayGetCount(windowList);
    for (CFIndex i = 0; i < count; i++) {
        auto dict = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(windowList, i));

        // 获取窗口 ID
        CGWindowID wid = 0;
        auto numRef = static_cast<CFNumberRef>(
            CFDictionaryGetValue(dict, kCGWindowNumber));
        if (numRef) {
            CFNumberGetValue(numRef, kCFNumberIntType, &wid);
        }
        if (wid == 0) continue;

        // 获取窗口名称
        auto nameRef = static_cast<CFStringRef>(
            CFDictionaryGetValue(dict, kCGWindowName));
        std::string title = cfStringToStdString(nameRef);

        // 获取窗口层级 — 只保留普通窗口（layer == 0）
        int layer = 0;
        auto layerRef = static_cast<CFNumberRef>(
            CFDictionaryGetValue(dict, kCGWindowLayer));
        if (layerRef) {
            CFNumberGetValue(layerRef, kCFNumberIntType, &layer);
        }
        if (layer != 0) continue;

        // 获取所属应用名称
        auto ownerRef = static_cast<CFStringRef>(
            CFDictionaryGetValue(dict, kCGWindowOwnerName));
        std::string ownerName = cfStringToStdString(ownerRef);

        // macOS 15+ 没有屏幕录制权限时 kCGWindowName 为空，
        // 用应用名称作为后备显示名
        if (title.empty()) {
            if (ownerName.empty()) continue;
            title = ownerName;
        }

        WindowInfo info;
        info.handle   = static_cast<NativeWindowHandle>(wid);
        info.title    = title;
        info.appName  = ownerName;
        windows.push_back(std::move(info));
    }

    CFRelease(windowList);
    return windows;
}

} // namespace navi
