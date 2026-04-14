#include "platform.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <filesystem>

namespace navi { namespace platform {

std::string getExecutableDir() {
    wchar_t buf[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return std::filesystem::path(buf).parent_path().string();
}

std::vector<std::string> getChineseFontPaths() {
    return {
        "C:\\Windows\\Fonts\\msyh.ttc",
        "C:\\Windows\\Fonts\\simhei.ttf",
    };
}

std::string wstringToUtf8(const std::wstring& wstr) {
    if (wstr.empty()) return {};
    int size = WideCharToMultiByte(
        CP_UTF8, 0,
        wstr.c_str(), static_cast<int>(wstr.size()),
        nullptr, 0, nullptr, nullptr);
    std::string result(size, '\0');
    WideCharToMultiByte(
        CP_UTF8, 0,
        wstr.c_str(), static_cast<int>(wstr.size()),
        result.data(), size, nullptr, nullptr);
    return result;
}

std::wstring utf8ToWstring(const std::string& str) {
    if (str.empty()) return {};
    int size = MultiByteToWideChar(
        CP_UTF8, 0,
        str.c_str(), static_cast<int>(str.size()),
        nullptr, 0);
    std::wstring result(size, L'\0');
    MultiByteToWideChar(
        CP_UTF8, 0,
        str.c_str(), static_cast<int>(str.size()),
        result.data(), size);
    return result;
}

}} // namespace navi::platform
