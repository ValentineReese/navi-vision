#include "platform.h"

#include <mach-o/dyld.h>
#include <climits>
#include <filesystem>
#include <codecvt>
#include <locale>

namespace navi { namespace platform {

std::string getExecutableDir() {
    char buf[PATH_MAX];
    uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) == 0) {
        return std::filesystem::path(buf).parent_path().string();
    }
    // Buffer too small — allocate dynamically
    std::vector<char> bigBuf(size);
    _NSGetExecutablePath(bigBuf.data(), &size);
    return std::filesystem::path(bigBuf.data()).parent_path().string();
}

std::vector<std::string> getChineseFontPaths() {
    return {
        "/System/Library/Fonts/PingFang.ttc",
        "/System/Library/Fonts/STHeiti Light.ttc",
        "/System/Library/Fonts/Hiragino Sans GB.ttc",
        "/Library/Fonts/Arial Unicode.ttf",
    };
}

std::string wstringToUtf8(const std::wstring& wstr) {
    if (wstr.empty()) return {};
    std::wstring_convert<std::codecvt_utf8<wchar_t>> conv;
    return conv.to_bytes(wstr);
}

std::wstring utf8ToWstring(const std::string& str) {
    if (str.empty()) return {};
    std::wstring_convert<std::codecvt_utf8<wchar_t>> conv;
    return conv.from_bytes(str);
}

}} // namespace navi::platform
