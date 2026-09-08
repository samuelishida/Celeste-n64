#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "n64/frame_arena.hpp"

namespace n64 {

// Measures frame time using timer_ticks().
// Reports a rolling average every N frames via debugf().
class FrameProfiler {
public:
    // Named per-phase timers. Each phase reports its own rolling average.
    // After removing the z-split distant + low-priority passes, the remaining
    // phases are contiguous.
    enum Phase {
        kPhaseHighPriority = 0,
        kPhaseTextureUpload,
        kPhaseStreaming,
        kPhaseUpdate,
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
    // Per-phase PEAK in ms over the current report window (streaming-experience
    // Inc 1): the max single-frame phase cost since the last report refresh.
    // A 60-frame AVERAGE dilutes a one-frame transition spike below visibility;
    // the done criterion for the async-load increment is a bounded peak, so the
    // report exposes it. Reset on each report refresh (same cadence as the avg).
    // Stored in ms (like `phase_average_ms`) so the header stays host-safe
    // (no N64 `TIMER_TICKS_LL` dependency in a host-included header).
    float phase_peak_ms(Phase phase) const { return phase_peak_ms_[phase]; }

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
    // Per-phase peak (single-frame max) in ms over the current window
    // (streaming-experience Inc 1). Computed in EndFrame from the tick peak
    // (where TIMER_TICKS_LL is available), reset on report refresh.
    float phase_peak_ms_[kPhaseCount] = {};
    uint64_t phase_peak_ticks_[kPhaseCount] = {};
};

// Captures heap statistics via mallinfo().
struct MemorySnapshot {
    // Named component buckets appended to the [memory] report line (memory-opt
    // Inc 1). Buckets report live per-component byte estimates (staging pool,
    // resident renderers, coll mesh, mappack structs, frame arena). They are
    // informational subtotals: `BucketBytes() <= total_bytes` in practice and
    // is enforced by the host contract test. Fixed capacity keeps the report
    // bounded.
    static constexpr int kMaxBuckets = 8;
    struct Bucket {
        const char* name = nullptr;
        size_t bytes = 0;
    };

    uint32_t total_bytes = 0;   // Total arena size
    uint32_t used_bytes = 0;    // Bytes in use
    uint32_t free_bytes = 0;    // Free bytes
    uint32_t largest_free = 0;  // Largest contiguous free block
    int bucket_count = 0;       // # live buckets (<= kMaxBuckets)
    Bucket buckets[kMaxBuckets] = {};

    // Host-safe bucket append (clips at kMaxBuckets; null name skipped;
    // never fails).
    void AddBucket(const char* name, size_t bytes) {
        if (name == nullptr || bucket_count >= kMaxBuckets) return;
        buckets[bucket_count].name = name;
        buckets[bucket_count].bytes = bytes;
        ++bucket_count;
    }

    // Sum of all live bucket bytes. Host-safe.
    size_t BucketBytes() const {
        size_t sum = 0;
        for (int i = 0; i < bucket_count; ++i) sum += buckets[i].bytes;
        return sum;
    }

    // Render the full `[memory]` line, parse-compatible with the legacy
    // `[memory] total=%u used=%u free=%u` prefix (buckets appended after, as
    // ` name=bytes`). Host-safe (uses snprintf only). Returns the number of
    // characters written (the would-be length when truncated to `cap`).
    size_t FormatLine(char* out, size_t cap) const {
        if (out == nullptr || cap == 0) return 0;
        const int head = std::snprintf(out, cap, "[memory] total=%u used=%u free=%u",
                                       static_cast<unsigned int>(total_bytes),
                                       static_cast<unsigned int>(used_bytes),
                                       static_cast<unsigned int>(free_bytes));
        if (head < 0) return 0;
        int n = head;
        for (int i = 0; i < bucket_count && static_cast<size_t>(n) < cap; ++i) {
            // NOTE: `%lu` + unsigned-long cast, NOT `%zu` — N64 newlib printf
            // does not support the `z` length modifier (prints literal "zu").
            // Values fit in 32 bits on N64; host printing is unaffected.
            const int extra = std::snprintf(out + n, cap - static_cast<size_t>(n),
                                            " %s=%lu", buckets[i].name,
                                            static_cast<unsigned long>(buckets[i].bytes));
            if (extra < 0) return static_cast<size_t>(n);
            n += extra;
        }
        // Truncation marker: if the line overran `cap`, replace the tail with
        // "..." (diagnostics line; the prefix is still parse-compatible).
        if (static_cast<size_t>(n) >= cap && cap >= 4) {
            out[cap - 4] = '.';
            out[cap - 3] = '.';
            out[cap - 2] = '.';
            out[cap - 1] = '\0';
        }
        return static_cast<size_t>(n);
    }

    // N64: print the full [memory] line via debugf (used by rom_main's
    // 3600-frame report).
    void PrintLine() const;

    static MemorySnapshot Capture();

    // Extend the snapshot with the frame-arena usage (Inc 7). `arena_used`
    // is the frame arena's current Used() bytes; `arena_capacity` its total.
    static MemorySnapshot CaptureWithArena(const FrameArena& arena);

    // Register a named bucket provider invoked at every Capture(). Providers
    // are N64-side (they read live subsystem state); a null name/provider or
    // a full registry is skipped. Call once before the first capture; the
    // provider list is additive (never clears).
    static void RegisterBucket(const char* name, size_t (*provider)(void));
};

}  // namespace n64
