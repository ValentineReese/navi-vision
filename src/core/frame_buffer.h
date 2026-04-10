#pragma once

#include <vector>
#include <memory>
#include <mutex>
#include <chrono>
#include <cstdint>

namespace navi {

/// 一帧画面数据（BGRA 格式，连续内存块，与 WGC 原生格式一致）
struct FrameData {
    std::vector<uint8_t> pixels;  // BGRA 像素数据（width * height * 4 字节）
    int width    = 0;             // 宽度（像素）
    int height   = 0;             // 高度（像素）
    int channels = 4;             // 通道数（BGRA = 4）
    std::chrono::steady_clock::time_point timestamp; // 捕获时间戳
};

/// 线程安全的帧缓冲区 —— 始终保留"最新一帧"
///
/// 设计思路：捕获线程不断覆盖写入最新帧数据，
/// 推理线程 / UI 线程可随时读取到"最新鲜"的画面。
/// 使用 std::mutex + shared_ptr 组合，锁粒度极小（仅交换指针）。
class FrameBuffer {
public:
    /// 写入最新帧（由捕获线程调用）
    void write(std::shared_ptr<FrameData> frame);

    /// 读取最新帧（由推理线程 / UI 线程调用）
    /// @return 最新帧的 shared_ptr，若尚无数据则返回 nullptr
    std::shared_ptr<FrameData> read() const;

    /// 清空缓冲区
    void clear();

private:
    mutable std::mutex mutex_;
    std::shared_ptr<FrameData> latest_;
};

} // namespace navi
