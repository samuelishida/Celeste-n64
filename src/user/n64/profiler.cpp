#include "profiler.hpp"

#include <debug.h>
#include <malloc.h>
#include <timer.h>

namespace n64 {

FrameProfiler::FrameProfiler(uint32_t report_interval)
    : report_interval_(report_interval), silent_(false) {}

void FrameProfiler::BeginFrame() {
    frame_start_ticks_ = timer_ticks();
    // Reset per-phase accumulation for this frame.
    for (int i = 0; i < kPhaseCount; ++i) {
        phase_accum_ticks_[i] = 0;
        phase_open_[i] = false;
    }
}

void FrameProfiler::BeginPhase(Phase phase) {
    if (phase < 0 || phase >= kPhaseCount) return;
    phase_open_[phase] = true;
    phase_start_ticks_[phase] = timer_ticks();
}

void FrameProfiler::EndPhase(Phase phase) {
    if (phase < 0 || phase >= kPhaseCount) return;
    if (!phase_open_[phase]) return;  // not open — ignore
    const uint64_t end = timer_ticks();
    const uint64_t elapsed = end > phase_start_ticks_[phase]
        ? (end - phase_start_ticks_[phase]) : 0;
    phase_accum_ticks_[phase] += elapsed;
    phase_open_[phase] = false;
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
        // TIMER_TICKS_LL(1000) returns ticks per millisecond; dividing by it
        // gives milliseconds directly. The previous formula multiplied by 1000
        // again and reported microseconds as milliseconds (e.g. 16700 "ms"
        // for a 16.7 ms frame).
        const float avg_ms = avg_ticks / static_cast<float>(TIMER_TICKS_LL(1000));
        last_average_ms_ = avg_ms;

        // Always refresh per-phase averages so `phase_average_ms()` is fresh
        // every report interval even when silent. Only the debugf self-print
        // is gated (rom_main is the single report path when silent).
        static const char* kPhaseNames[kPhaseCount] = {
            "high_priority", "texture_upload", "streaming",
        };
        for (int i = 0; i < kPhaseCount; ++i) {
            const float p_avg = (static_cast<float>(phase_accum_ticks_[i]) /
                                  static_cast<float>(frame_count_)) /
                                static_cast<float>(TIMER_TICKS_LL(1000));
            phase_avg_ms_[i] = p_avg;
        }

        if (!silent_) {
            debugf("[profiler] avg frame time over %u frames: %.3f ms (%.1f fps)\n",
                   static_cast<unsigned int>(frame_count_), avg_ms,
                   avg_ms > 0.0f ? (1000.0f / avg_ms) : 0.0f);
            for (int i = 0; i < kPhaseCount; ++i) {
                debugf("[profiler]   %-14s %.3f ms\n", kPhaseNames[i],
                       phase_avg_ms_[i]);
            }
        }

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

MemorySnapshot MemorySnapshot::CaptureWithArena(const FrameArena& arena) {
    MemorySnapshot snap = Capture();
    // Report the frame-arena usage alongside heap stats (arch.md §30).
    // We fold the arena's used bytes into the "used" figure and its remaining
    // into "free" so the memory report reflects both the heap and the frame
    // pool. The arena capacity is reported as part of total.
    snap.total_bytes += static_cast<uint32_t>(FrameArena::kArenaSize);
    snap.used_bytes += static_cast<uint32_t>(arena.Used());
    snap.free_bytes += static_cast<uint32_t>(arena.Remaining());
    return snap;
}

}  // namespace n64
