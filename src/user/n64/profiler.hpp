#pragma once

#include <cstdint>

namespace n64 {

// Measures frame time using timer_ticks().
// Reports a rolling average every N frames via debugf().
class FrameProfiler {
public:
    explicit FrameProfiler(uint32_t report_interval = 60);

    // Call once per frame, before any work.
    void BeginFrame();
    // Call once per frame, after all work.
    void EndFrame();

    // Last computed average in milliseconds.
    float last_average_ms() const { return last_average_ms_; }

private:
    uint32_t report_interval_;
    uint32_t frame_count_ = 0;
    uint64_t frame_start_ticks_ = 0;
    uint64_t accumulated_ticks_ = 0;
    float last_average_ms_ = 0.0f;
};

// Captures heap statistics via mallinfo().
struct MemorySnapshot {
    uint32_t total_bytes = 0;   // Total arena size
    uint32_t used_bytes = 0;    // Bytes in use
    uint32_t free_bytes = 0;    // Free bytes
    uint32_t largest_free = 0;  // Largest contiguous free block

    static MemorySnapshot Capture();
};

}  // namespace n64
