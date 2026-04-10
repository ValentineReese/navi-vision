#pragma once

#include <string>
#include <vector>
#include <functional>
#include <atomic>

namespace navi {

/// HuggingFace 模型条目（预设或自定义）
struct ModelEntry {
    std::string name;            // 显示名称
    std::string description;     // 简要描述（参数量、量化级别、显存需求）
    std::string model_url;       // HuggingFace 下载 URL — 主模型
    std::string mmproj_url;      // HuggingFace 下载 URL — 视觉投影器
    std::string model_filename;  // 本地文件名
    std::string mmproj_filename; // 本地文件名
};

/// 模型管理工具类 — 无状态，全静态方法
class ModelManager {
public:
    using ProgressCallback = std::function<void(size_t downloaded, size_t total)>;

    /// 返回预设的 HuggingFace 模型列表（含 Qwen2.5-VL 系列）
    static std::vector<ModelEntry> getDefaultModels();

    /// 获取 models 目录的绝对路径（exe 同级的 models/ 子目录）
    static std::string getModelsDir();

    /// 获取指定条目的本地文件完整路径
    static std::string getModelPath(const ModelEntry& entry);
    static std::string getMmprojPath(const ModelEntry& entry);

    /// 检查指定模型的两个文件是否都已下载
    static bool isModelReady(const ModelEntry& entry);

    /// 从 URL 中提取文件名（去除查询参数）
    static std::string filenameFromUrl(const std::string& url);

    /// 格式化字节数为人类可读字符串
    static std::string formatBytes(size_t bytes);

    /// 下载单个文件（支持 HTTPS，自动跟随重定向，可选 HTTP 代理）
    /// @param url       完整下载 URL
    /// @param destPath  本地保存路径
    /// @param progress  进度回调 (已下载字节, 总字节)；总字节可能为 0（未知）
    /// @param cancelled 外部取消信号
    /// @param errorOut  失败时写入错误详情
    /// @param proxy     HTTP 代理地址，如 "127.0.0.1:7890"；空字符串表示不使用代理
    /// @return true 表示下载成功
    static bool downloadFile(
        const std::string& url,
        const std::string& destPath,
        ProgressCallback progress,
        std::atomic<bool>& cancelled,
        std::string& errorOut,
        const std::string& proxy = "");
};

} // namespace navi
