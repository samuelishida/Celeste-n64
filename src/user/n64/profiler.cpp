#include "profiler.hpp"

#include <debug.h>
#include <malloc.h>
#include <timer.h>

namespace n64 {

FrameProfiler::FrameProfiler(uint32_t report_interval)
    : report_interval_(report_interval) {}

void FrameProfiler::BeginFrame() {
    frame_start_ticks_ = timer_ticks();
}

void FrameProfiler::EndFrame() {
    const uint64_t end_ticks = timer_ticks();
    const uint64_t elapsed = end_ticks > frame_start_ticks_
        ? (end_ticks - frame_start_ticks_)
        : 0;
    accumulated_ticks_ += elapsed;
    ++frame_count_;

    if (frame_count_ >= report_interval_) {
        const float avg_ticks = static_cast<float>(accumulated_ticks_) / static_cast<float>(frame_count_);
        const float avg_ms = (avg_ticks * 1000.0f) / static_cast<float>(TIMER_TICKS_LL(1000));
        last_average_ms_ = avg_ms;

        debugf("[profiler] avg frame time over %u frames: %.3f ms (%.1f fps)\n",
               static_cast<unsigned int>(frame_count_), avg_ms,
               avg_ms > 0.0f ? (1000.0f / avg_ms) : 0.0f);

        frame_count_ = 0;
        accumulated_ticks_ = 0;
    }
}

MemorySnapshot MemorySnapshot::Capture() {
    struct mallinfo info = mallinfo();
    MemorySnapshot snap;
    snap.total_bytes = static_cast<uint32_t>(info.arena);
    snap.used_bytes = static_cast<uint32_t>(info.uordblks);
    snap.free_bytes = static_cast<uint32_t>(info.fordblks);
    // mallinfo doesn't give largest free block directly; we approximate
    // by reporting free_bytes as an upper bound for largest contiguous.
    snap.largest_free = snap.free_bytes;
    return snap;
}

}  // namespace n64
