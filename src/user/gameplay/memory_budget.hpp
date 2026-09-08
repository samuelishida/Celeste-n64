// memory-opt Inc 5: host-safe memory budget registry matching the Inc 1
// [memory] buckets. Registers a byte cap per component; `Check` emits a
// debug-only `[budget] WARN` line for every over-cap bucket. NEVER a release
// hard-fail (user-confirmed Q3) — the warn is telemetry for the budget doc.
//
// Host-safe: no N64 types. The contract test exercises it in `make test`.
#ifndef MADELINE_CUBE_MEMORY_BUDGET_HPP
#define MADELINE_CUBE_MEMORY_BUDGET_HPP

#include <cstddef>
#include <cstdio>
#include <cstring>

#include "n64/profiler.hpp"  // MemorySnapshot

namespace madeline_cube {

struct BudgetCap {
    const char* name;  // must match a MemorySnapshot bucket name
    size_t cap;        // byte cap
};

class BudgetTracker {
public:
    static constexpr int kMaxCaps = 8;

    // Register a cap (bounded at kMaxCaps; null name skipped; never fails).
    void RegisterCap(const BudgetCap& cap) {
        if (cap.name == nullptr || cap_count_ >= kMaxCaps) return;
        caps_[cap_count_] = cap;
        ++cap_count_;
    }

    // Check the snapshot against every registered cap. For each over-cap
    // bucket, appends a `[budget] WARN <name>=<bytes> cap=<cap>` line to
    // `out` (bounded by `out_cap`). Returns the number of warnings emitted.
    // Debug-only warn — the caller decides what to do with the line.
    int Check(const n64::MemorySnapshot& snap, char* out, size_t out_cap) const {
        int warns = 0;
        if (out == nullptr || out_cap == 0) return 0;
        out[0] = '\0';
        size_t used = 0;
        for (int c = 0; c < cap_count_; ++c) {
            // Find the matching bucket by name.
            size_t bytes = 0;
            bool found = false;
            for (int b = 0; b < snap.bucket_count; ++b) {
                if (snap.buckets[b].name != nullptr &&
                    std::strcmp(snap.buckets[b].name, caps_[c].name) == 0) {
                    bytes = snap.buckets[b].bytes;
                    found = true;
                    break;
                }
            }
            if (!found) continue;  // bucket not registered this run — skip
            if (bytes <= caps_[c].cap) continue;
            // NOTE: `%lu` + unsigned-long cast, NOT `%zu` — N64 newlib printf
            // does not support the `z` length modifier.
            const int n = std::snprintf(
                out + used, out_cap - used, "[budget] WARN %s=%lu cap=%lu\n",
                caps_[c].name, static_cast<unsigned long>(bytes),
                static_cast<unsigned long>(caps_[c].cap));
            if (n < 0) break;
            used += static_cast<size_t>(n);
            if (used >= out_cap) break;
            ++warns;
        }
        return warns;
    }

    int CapCount() const { return cap_count_; }

private:
    BudgetCap caps_[kMaxCaps] = {};
    int cap_count_ = 0;
};

}  // namespace madeline_cube

#endif  // MADELINE_CUBE_MEMORY_BUDGET_HPP
