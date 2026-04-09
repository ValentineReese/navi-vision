#include "window_enumerator.h"
#include <dwmapi.h>

#pragma comment(lib, "dwmapi.lib")

namespace navi {

std::vector<WindowInfo> WindowEnumerator::enumerate() {
    std::vector<WindowInfo> windows;
    // EnumWindows 会对每个顶层窗口调用回调函数
    EnumWindows(enumCallback, reinterpret_cast<LPARAM>(&windows));
    return windows;
}

BOOL CALLBACK WindowEnumerator::enumCallback(HWND hwnd, LPARAM lParam) {
    auto* windows = reinterpret_cast<std::vector<WindowInfo>*>(lParam);

    if (!isValidWindow(hwnd))
        return TRUE; // 继续枚举下一个窗口

    // 获取窗口标题
    wchar_t title[256] = {};
    GetWindowTextW(hwnd, title, 256);
    if (wcslen(title) == 0)
        return TRUE;

    // 获取窗口类名（用于调试和过滤）
    wchar_t className[256] = {};
    GetClassNameW(hwnd, className, 256);

    windows->push_back({ hwnd, title, className });
    return TRUE;
}

bool WindowEnumerator::isValidWindow(HWND hwnd) {
    // ① 过滤不可见窗口
    if (!IsWindowVisible(hwnd))
        return false;

    // ② 过滤无标题窗口
    if (GetWindowTextLengthW(hwnd) == 0)
        return false;

    // ③ 过滤有 Owner 的弹出窗口（只保留主窗口）
    if (GetWindow(hwnd, GW_OWNER) != nullptr)
        return false;

    // ④ 过滤被 DWM "cloaked"（隐藏）的窗口
    //    例如 UWP 应用的后台窗口会被标记为 cloaked
    DWORD cloaked = 0;
    DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
    if (cloaked)
        return false;

    // ⑤ 过滤工具窗口（WS_EX_TOOLWINDOW）
    LONG exStyle = GetWindowLongW(hwnd, GWL_EXSTYLE);
    if (exStyle & WS_EX_TOOLWINDOW)
        return false;

    return true;
}

} // namespace navi
