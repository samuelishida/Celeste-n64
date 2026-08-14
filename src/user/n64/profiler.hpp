#pragma once

#include <cstdint>

#include "n64/frame_arena.hpp"

namespace n64 {

// Measures frame time using timer_ticks().
// Reports a rolling average every N frames via debugf().
class FrameProfiler {
public:
    // Named per-phase timers (arch.md §21 / Inc 7). Each phase reports its
    // own rolling average.
    enum Phase {
        kPhaseDistant = 0,
        kPhaseLowPriority,
        kPhaseHighPriority,
        kPhaseParticles,
        kPhaseTextureUpload,
        kPhaseStreaming,
        kPhaseCount,
    };

    explicit FrameProfiler(uint32_t report_interval = 60);

    // Call once per frame, before any work.
    void BeginFrame();
    // Call once per frame, after all work.
    void EndFrame();

    // Suppress the self-print `[profiler]` debugf lines. Averages still
    // accumulate + refresh at `report_interval_` (so `phase_average_ms()`
    // stays fresh); only the debugf output is silenced. Used when a caller
    // (rom_main) reads `phase_average_ms()` as the single reporting path.
    void SetSilent(bool silent) { silent_ = silent; }

    // Mark the start/end of a named phase. An unclosed phase asserts at frame
    // end (catches renderer bugs).
    void BeginPhase(Phase phase);
    void EndPhase(Phase phase);

    // Last computed average in milliseconds.
    float last_average_ms() const { return last_average_ms_; }
    // Rolling average ms for a phase (0 if not yet reported).
    float phase_average_ms(Phase phase) const { return phase_avg_ms_[phase]; }

private:
    uint32_t report_interval_;
    bool silent_ = false;
    uint32_t frame_count_ = 0;
    uint64_t frame_start_ticks_ = 0;
    uint64_t accumulated_ticks_ = 0;
    float last_average_ms_ = 0.0f;

    // Per-phase accumulation.
    uint64_t phase_start_ticks_[kPhaseCount] = {};
    uint64_t phase_accum_ticks_[kPhaseCount] = {};
    bool phase_open_[kPhaseCount] = {};
    float phase_avg_ms_[kPhaseCount] = {};
};

// Captures heap statistics via mallinfo().
struct MemorySnapshot {
    uint32_t total_bytes = 0;   // Total arena size
    uint32_t used_bytes = 0;    // Bytes in use
    uint32_t free_bytes = 0;    // Free bytes
    uint32_t largest_free = 0;  // Largest contiguous free block

    static MemorySnapshot Capture();

    // Extend the snapshot with the frame-arena usage (Inc 7). `arena_used`
    // is the frame arena's current Used() bytes; `arena_capacity` its total.
    static MemorySnapshot CaptureWithArena(const FrameArena& arena);
};

}  // namespace n64
