#include "frame_buffer.h"

namespace navi {

void FrameBuffer::write(std::shared_ptr<FrameData> frame) {
    // 加锁后仅做指针交换，临界区极短
    std::lock_guard<std::mutex> lock(mutex_);
    latest_ = std::move(frame);
}

std::shared_ptr<FrameData> FrameBuffer::read() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return latest_;
}

void FrameBuffer::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    latest_.reset();
}

} // namespace navi
