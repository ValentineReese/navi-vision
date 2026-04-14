#include "model_manager.h"
#include "../platform/platform.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <wininet.h>
#pragma comment(lib, "wininet.lib")
#else
#include <curl/curl.h>
#endif

#include <filesystem>
#include <fstream>
#include <cstdio>

namespace navi {

// ============================================================
//  HuggingFace 预设模型列表
//  URL 格式: https://huggingface.co/{org}/{repo}/resolve/main/{filename}
// ============================================================
std::vector<ModelEntry> ModelManager::getDefaultModels() {
    return {
        {
            "Qwen2.5-VL-3B Instruct Q4_K_M",
            "3B, 4-bit, ~1.9 GB (Mungert)",
            "https://huggingface.co/Mungert/Qwen2.5-VL-3B-Instruct-GGUF/resolve/main/Qwen2.5-VL-3B-Instruct-q4_k_m.gguf",
            "https://huggingface.co/Mungert/Qwen2.5-VL-3B-Instruct-GGUF/resolve/main/Qwen2.5-VL-3B-Instruct-mmproj-f16.gguf",
            "Qwen2.5-VL-3B-Instruct-q4_k_m.gguf",
            "Qwen2.5-VL-3B-Instruct-mmproj-f16.gguf"
        },
        {
            "Qwen2.5-VL-3B Instruct Q8_0",
            "3B, 8-bit, ~3.3 GB (Mungert)",
            "https://huggingface.co/Mungert/Qwen2.5-VL-3B-Instruct-GGUF/resolve/main/Qwen2.5-VL-3B-Instruct-q8_0.gguf",
            "https://huggingface.co/Mungert/Qwen2.5-VL-3B-Instruct-GGUF/resolve/main/Qwen2.5-VL-3B-Instruct-mmproj-f16.gguf",
            "Qwen2.5-VL-3B-Instruct-q8_0.gguf",
            "Qwen2.5-VL-3B-Instruct-mmproj-f16.gguf"
        },
        {
            "Qwen2.5-VL-7B Instruct Q4_K_M",
            "7B, 4-bit, ~4.4 GB (unsloth)",
            "https://huggingface.co/unsloth/Qwen2.5-VL-7B-Instruct-GGUF/resolve/main/Qwen2.5-VL-7B-Instruct-Q4_K_M.gguf",
            "https://huggingface.co/unsloth/Qwen2.5-VL-7B-Instruct-GGUF/resolve/main/mmproj-BF16.gguf",
            "Qwen2.5-VL-7B-Instruct-Q4_K_M.gguf",
            "Qwen2.5-VL-7B-mmproj-BF16.gguf"
        },
        {
            "Qwen2.5-VL-7B Instruct Q8_0",
            "7B, 8-bit, ~7.5 GB (unsloth)",
            "https://huggingface.co/unsloth/Qwen2.5-VL-7B-Instruct-GGUF/resolve/main/Qwen2.5-VL-7B-Instruct-Q8_0.gguf",
            "https://huggingface.co/unsloth/Qwen2.5-VL-7B-Instruct-GGUF/resolve/main/mmproj-BF16.gguf",
            "Qwen2.5-VL-7B-Instruct-Q8_0.gguf",
            "Qwen2.5-VL-7B-mmproj-BF16.gguf"
        },
        {
            "MiniCPM-o-4.5 Q4_K_M",
            "8B, 4-bit, ~4.7 GB + 1.0 GB vision (openbmb)",
            "https://huggingface.co/openbmb/MiniCPM-o-4_5-gguf/resolve/main/MiniCPM-o-4_5-Q4_K_M.gguf",
            "https://huggingface.co/openbmb/MiniCPM-o-4_5-gguf/resolve/main/vision/MiniCPM-o-4_5-vision-F16.gguf",
            "MiniCPM-o-4_5-Q4_K_M.gguf",
            "MiniCPM-o-4_5-vision-F16.gguf"
        },
        {
            "MiniCPM-o-4.5 Q8_0",
            "8B, 8-bit, ~8.1 GB + 1.0 GB vision (openbmb)",
            "https://huggingface.co/openbmb/MiniCPM-o-4_5-gguf/resolve/main/MiniCPM-o-4_5-Q8_0.gguf",
            "https://huggingface.co/openbmb/MiniCPM-o-4_5-gguf/resolve/main/vision/MiniCPM-o-4_5-vision-F16.gguf",
            "MiniCPM-o-4_5-Q8_0.gguf",
            "MiniCPM-o-4_5-vision-F16.gguf"
        }
    };
}

// ============================================================
//  路径工具
// ============================================================

std::string ModelManager::getModelsDir() {
    auto dir = std::filesystem::path(platform::getExecutableDir()) / "models";
    std::filesystem::create_directories(dir);
    return dir.string();
}

std::string ModelManager::getModelPath(const ModelEntry& entry) {
    return (std::filesystem::path(getModelsDir()) / entry.model_filename).string();
}

std::string ModelManager::getMmprojPath(const ModelEntry& entry) {
    return (std::filesystem::path(getModelsDir()) / entry.mmproj_filename).string();
}

bool ModelManager::isModelReady(const ModelEntry& entry) {
    return std::filesystem::exists(getModelPath(entry)) &&
           std::filesystem::exists(getMmprojPath(entry));
}

std::string ModelManager::filenameFromUrl(const std::string& url) {
    std::string clean = url;
    // 去除查询参数
    auto qpos = clean.find('?');
    if (qpos != std::string::npos) clean = clean.substr(0, qpos);
    // 取最后一段路径
    auto spos = clean.rfind('/');
    return (spos != std::string::npos) ? clean.substr(spos + 1) : "model.gguf";
}

std::string ModelManager::formatBytes(size_t bytes) {
    char buf[64];
    if (bytes < 1024) {
        snprintf(buf, sizeof(buf), "%zu B", bytes);
    } else if (bytes < 1024 * 1024) {
        snprintf(buf, sizeof(buf), "%.1f KB", bytes / 1024.0);
    } else if (bytes < 1024ULL * 1024 * 1024) {
        snprintf(buf, sizeof(buf), "%.1f MB", bytes / (1024.0 * 1024));
    } else {
        snprintf(buf, sizeof(buf), "%.2f GB", bytes / (1024.0 * 1024 * 1024));
    }
    return buf;
}

// ============================================================
//  平台相关下载实现
// ============================================================

#ifdef _WIN32
// ============================================================
//  WinINet 句柄 RAII 包装 (Windows)
// ============================================================
struct INetHandle {
    HINTERNET h = nullptr;
    ~INetHandle() { if (h) InternetCloseHandle(h); }
    explicit operator bool() const { return h != nullptr; }
};

static std::string getWinINetError(DWORD err) {
    char buf[512];
    DWORD len = FormatMessageA(
        FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_FROM_HMODULE,
        GetModuleHandleA("wininet.dll"),
        err, 0, buf, sizeof(buf), nullptr);
    if (len > 0) {
        while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
            buf[--len] = '\0';
        return std::string(buf, len);
    }
    snprintf(buf, sizeof(buf), "WinINet error %lu", err);
    return buf;
}

// ============================================================
//  文件下载 — Windows WinINet 实现
// ============================================================
bool ModelManager::downloadFile(
    const std::string& url,
    const std::string& destPath,
    ProgressCallback progress,
    std::atomic<bool>& cancelled,
    std::string& errorOut,
    const std::string& proxy)
{
    errorOut.clear();

    // 确保父目录存在
    std::filesystem::create_directories(
        std::filesystem::path(destPath).parent_path());

    // 打开 Internet 会话（支持 HTTP 代理）
    INetHandle session;
    if (!proxy.empty()) {
        session.h = InternetOpenA(
            "NaviVision/1.0",
            INTERNET_OPEN_TYPE_PROXY,
            proxy.c_str(), nullptr, 0);
    } else {
        session.h = InternetOpenA(
            "NaviVision/1.0",
            INTERNET_OPEN_TYPE_PRECONFIG,
            nullptr, nullptr, 0);
    }
    if (!session) {
        errorOut = "InternetOpen failed: " + getWinINetError(GetLastError());
        return false;
    }

    // 超时设置
    DWORD connectTimeout = 30000;
    DWORD sendTimeout    = 30000;
    DWORD receiveTimeout = 60000;
    InternetSetOptionA(session.h, INTERNET_OPTION_CONNECT_TIMEOUT,
                       &connectTimeout, sizeof(connectTimeout));
    InternetSetOptionA(session.h, INTERNET_OPTION_SEND_TIMEOUT,
                       &sendTimeout, sizeof(sendTimeout));
    InternetSetOptionA(session.h, INTERNET_OPTION_RECEIVE_TIMEOUT,
                       &receiveTimeout, sizeof(receiveTimeout));

    // 打开 URL — HTTPS 安全标记 + 跟随重定向
    INetHandle hUrl;
    hUrl.h = InternetOpenUrlA(
        session.h, url.c_str(), nullptr, 0,
        INTERNET_FLAG_RELOAD |
        INTERNET_FLAG_NO_CACHE_WRITE |
        INTERNET_FLAG_SECURE |
        INTERNET_FLAG_IGNORE_REDIRECT_TO_HTTPS |
        INTERNET_FLAG_KEEP_CONNECTION,
        0);
    if (!hUrl) {
        DWORD err = GetLastError();
        errorOut = "InternetOpenUrl failed: " + getWinINetError(err)
                 + " (code " + std::to_string(err) + ")";
        return false;
    }

    // 检查 HTTP 状态码
    DWORD statusCode = 0;
    DWORD statusLen = sizeof(statusCode);
    HttpQueryInfoA(hUrl.h, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER,
                   &statusCode, &statusLen, nullptr);
    if (statusCode != 200 && statusCode != 0) {
        // 读取响应体前几百字节作为错误信息
        char errBody[512] = {};
        DWORD errRead = 0;
        InternetReadFile(hUrl.h, errBody, sizeof(errBody) - 1, &errRead);
        errorOut = "HTTP " + std::to_string(statusCode);
        if (errRead > 0) {
            std::string body(errBody, errRead);
            // 截取前 200 字符
            if (body.size() > 200) body = body.substr(0, 200) + "...";
            errorOut += " - " + body;
        }
        return false;
    }

    // 获取 Content-Length（可能为 0 = 服务器未返回）
    DWORD contentLength = 0;
    DWORD clLen = sizeof(contentLength);
    HttpQueryInfoA(hUrl.h, HTTP_QUERY_CONTENT_LENGTH | HTTP_QUERY_FLAG_NUMBER,
                   &contentLength, &clLen, nullptr);

    // 写入临时文件（下载完成后重命名，避免半成品文件）
    std::string tempPath = destPath + ".part";
    std::ofstream outFile(tempPath, std::ios::binary);
    if (!outFile.is_open()) {
        errorOut = "Cannot create file: " + tempPath;
        return false;
    }

    char buffer[65536];
    DWORD bytesRead = 0;
    size_t totalRead = 0;

    while (true) {
        if (cancelled.load()) {
            outFile.close();
            std::error_code ec;
            std::filesystem::remove(tempPath, ec);
            errorOut = "Download cancelled by user";
            return false;
        }

        if (!InternetReadFile(hUrl.h, buffer, sizeof(buffer), &bytesRead)) {
            DWORD err = GetLastError();
            outFile.close();
            std::error_code ec;
            std::filesystem::remove(tempPath, ec);
            errorOut = "Read error after " + formatBytes(totalRead)
                     + ": " + getWinINetError(err);
            return false;
        }

        if (bytesRead == 0) break;  // EOF

        outFile.write(buffer, static_cast<std::streamsize>(bytesRead));
        if (!outFile.good()) {
            outFile.close();
            std::error_code ec;
            std::filesystem::remove(tempPath, ec);
            errorOut = "File write error after " + formatBytes(totalRead);
            return false;
        }

        totalRead += bytesRead;
        if (progress) {
            progress(totalRead, static_cast<size_t>(contentLength));
        }
    }

    outFile.close();

    // 一致性检查：如果服务器返回了 Content-Length，验证下载的字节数
    if (contentLength > 0 && totalRead != static_cast<size_t>(contentLength)) {
        std::error_code ec;
        std::filesystem::remove(tempPath, ec);
        errorOut = "Size mismatch: expected " + formatBytes(contentLength)
                 + " got " + formatBytes(totalRead);
        return false;
    }

    // 原子重命名：temp → final
    std::error_code ec;
    std::filesystem::remove(destPath, ec);
    std::filesystem::rename(tempPath, destPath, ec);
    if (ec) {
        errorOut = "Rename failed: " + ec.message();
        return false;
    }
    return true;
}

#else  // macOS / Linux — libcurl 实现

// ============================================================
//  libcurl 回调数据
// ============================================================
struct CurlWriteData {
    std::ofstream* outFile;
    size_t totalWritten;
    std::atomic<bool>* cancelled;
    ModelManager::ProgressCallback progress;
    size_t contentLength;
};

static size_t curlWriteCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* data = static_cast<CurlWriteData*>(userdata);
    size_t bytes = size * nmemb;

    if (data->cancelled && data->cancelled->load()) {
        return 0;  // 返回 0 中止传输
    }

    data->outFile->write(ptr, static_cast<std::streamsize>(bytes));
    if (!data->outFile->good()) return 0;

    data->totalWritten += bytes;
    if (data->progress) {
        data->progress(data->totalWritten, data->contentLength);
    }
    return bytes;
}

static int curlProgressCallback(void* clientp, curl_off_t dltotal, curl_off_t dlnow,
                                 curl_off_t /*ultotal*/, curl_off_t /*ulnow*/) {
    auto* cancelled = static_cast<std::atomic<bool>*>(clientp);
    if (cancelled && cancelled->load()) return 1;  // 非零中止
    return 0;
}

// ============================================================
//  文件下载 — macOS/Linux libcurl 实现
// ============================================================
bool ModelManager::downloadFile(
    const std::string& url,
    const std::string& destPath,
    ProgressCallback progress,
    std::atomic<bool>& cancelled,
    std::string& errorOut,
    const std::string& proxy)
{
    errorOut.clear();

    std::filesystem::create_directories(
        std::filesystem::path(destPath).parent_path());

    std::string tempPath = destPath + ".part";
    std::ofstream outFile(tempPath, std::ios::binary);
    if (!outFile.is_open()) {
        errorOut = "Cannot create file: " + tempPath;
        return false;
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        errorOut = "curl_easy_init failed";
        outFile.close();
        return false;
    }

    CurlWriteData writeData{};
    writeData.outFile       = &outFile;
    writeData.totalWritten  = 0;
    writeData.cancelled     = &cancelled;
    writeData.progress      = progress;
    writeData.contentLength = 0;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &writeData);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 60L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "NaviVision/1.0");
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, curlProgressCallback);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &cancelled);

    if (!proxy.empty()) {
        curl_easy_setopt(curl, CURLOPT_PROXY, proxy.c_str());
    }

    CURLcode res = curl_easy_perform(curl);

    outFile.close();

    if (res != CURLE_OK) {
        std::error_code ec;
        std::filesystem::remove(tempPath, ec);
        if (cancelled.load()) {
            errorOut = "Download cancelled by user";
        } else {
            errorOut = std::string("curl error: ") + curl_easy_strerror(res);
        }
        curl_easy_cleanup(curl);
        return false;
    }

    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
    curl_easy_cleanup(curl);

    if (httpCode != 200) {
        std::error_code ec;
        std::filesystem::remove(tempPath, ec);
        errorOut = "HTTP " + std::to_string(httpCode);
        return false;
    }

    // 原子重命名
    std::error_code ec;
    std::filesystem::remove(destPath, ec);
    std::filesystem::rename(tempPath, destPath, ec);
    if (ec) {
        errorOut = "Rename failed: " + ec.message();
        return false;
    }
    return true;
}

#endif  // _WIN32

} // namespace navi
