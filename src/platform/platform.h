#pragma once

// ============================================================
//  跨平台工具函数声明
//
//  每个平台的实现在对应的 .cpp / .mm 文件中。
// ============================================================

#include <string>
#include <vector>

namespace navi { namespace platform {

/// 获取当前可执行文件所在目录的绝对路径（末尾不含分隔符）
std::string getExecutableDir();

/// 获取系统中文字体路径列表（优先级从高到低）
std::vector<std::string> getChineseFontPaths();

/// 宽字符串 → UTF-8 转换（Windows 使用 WideCharToMultiByte，macOS UTF-8 原生）
std::string wstringToUtf8(const std::wstring& wstr);

/// UTF-8 → 宽字符串转换
std::wstring utf8ToWstring(const std::string& str);

}} // namespace navi::platform
