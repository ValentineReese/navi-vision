#pragma once

#include <string>
#include <vector>
#include <cstdint>

#ifdef _WIN32
#include <Windows.h>
#endif

#include "capture_engine.h"   // NativeWindowHandle

namespace navi {

/// 窗口信息结构体（跨平台）
struct WindowInfo {
    NativeWindowHandle handle = kInvalidWindowHandle;  // 原生窗口句柄
    std::string title;      // 窗口标题 (UTF-8)
    std::string appName;    // 应用名称 / 窗口类名 (UTF-8)

#ifdef _WIN32
    // Windows 兼容：保留 HWND 访问
    HWND hwnd() const { return reinterpret_cast<HWND>(handle); }
    static WindowInfo fromHwnd(HWND h, const std::string& t, const std::string& cls) {
        WindowInfo info;
        info.handle  = reinterpret_cast<NativeWindowHandle>(h);
        info.title   = t;
        info.appName = cls;
        return info;
    }
#endif
};

/// 系统窗口枚举器
/// 枚举当前桌面上所有可见的主窗口，过滤掉系统隐藏/工具窗口
class WindowEnumerator {
public:
    /// 返回当前所有有效的可见主窗口列表
    static std::vector<WindowInfo> enumerate();

#ifdef _WIN32
private:
    static BOOL CALLBACK enumCallback(HWND hwnd, LPARAM lParam);
    static bool isValidWindow(HWND hwnd);
#endif
};

} // namespace navi
