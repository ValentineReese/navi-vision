#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <cstdarg>
#include <cstdio>
#include <chrono>

namespace navi {

// ============================================================
//  LogBuffer — 线程安全的内存日志缓冲区
//
//  任何线程都可以调用 add() 写入日志，UI 线程通过
//  getLines() 读取所有条目用于渲染。
//  全局单例通过 LogBuffer::instance() 获取。
// ============================================================
class LogBuffer {
public:
    static LogBuffer& instance() {
        static LogBuffer s;
        return s;
    }

    /// 添加一条日志（线程安全）
    void add(const std::string& msg) {
        std::lock_guard<std::mutex> lock(mutex_);
        lines_.push_back(msg);
        if (lines_.size() > maxLines_) {
            lines_.erase(lines_.begin(),
                         lines_.begin() + static_cast<int>(lines_.size() - maxLines_));
        }
        scrollToBottom_ = true;
    }

    /// printf 风格添加日志（线程安全）
    void addf(const char* fmt, ...) {
        char buf[2048];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        add(std::string(buf));
    }

    /// 获取所有日志行的拷贝（线程安全）
    std::vector<std::string> getLines() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return lines_;
    }

    /// 清空日志
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        lines_.clear();
    }

    /// 是否需要滚动到底部（UI 读取后重置）
    bool shouldScrollToBottom() {
        bool v = scrollToBottom_;
        scrollToBottom_ = false;
        return v;
    }

private:
    LogBuffer() = default;
    mutable std::mutex mutex_;
    std::vector<std::string> lines_;
    size_t maxLines_ = 500;
    bool scrollToBottom_ = false;
};

/// 便捷宏
#define NAVI_LOG(fmt, ...) ::navi::LogBuffer::instance().addf(fmt, ##__VA_ARGS__)

} // namespace navi
