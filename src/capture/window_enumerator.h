#pragma once

#include <string>
#include <vector>
#include <Windows.h>

namespace navi {

/// 窗口信息结构体
struct WindowInfo {
    HWND         hwnd;       // 窗口句柄
    std::wstring title;      // 窗口标题
    std::wstring className;  // 窗口类名
};

/// 系统窗口枚举器
/// 枚举当前桌面上所有可见的主窗口，过滤掉系统隐藏/工具窗口
class WindowEnumerator {
public:
    /// 返回当前所有有效的可见主窗口列表
    static std::vector<WindowInfo> enumerate();

private:
    static BOOL CALLBACK enumCallback(HWND hwnd, LPARAM lParam);
    static bool isValidWindow(HWND hwnd);
};

} // namespace navi
