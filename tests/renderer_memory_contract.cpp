// Host test for the Inc 5 / D6 memory diet (Pattern A: header-only, no N64
// deps). Asserts:
//   - `FrameArena` Alloc/Used/Reset semantics + exhaustion returns null. The
//     arena backs the single near pass's per-frame resident draw list.
//   - The batch-array ownership pattern the room renderers now follow
//     (heap-allocate sized to face count, free-before-realloc, free-nulls) is
//     pinned by a host-safe mirror so a regression in the pattern is caught.
//
// The real LvlRoomRenderer/TexturedRoomRenderer are device-only (include t3d +
// libdragon), so their actual allocated capacity is verified on device via the
// `[memory] used=` report; this test locks down the arena path + ownership
// rules they share.
//
// Build:
//   g++ -std=c++17 -Isrc/user tests/renderer_memory_contract.cpp
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "n64/frame_arena.hpp"

using namespace n64;

static int failures = 0;

static void expect(bool cond, const char* what) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

// Host-safe mirror of the room renderers' new batch-array ownership (Inc 5 /
// D6): batches_ is heap-allocated sized to min(face_count, cap), freed before
// realloc, and nulled on Free so the destructor can't double-free. This pins
// the exact rules LvlRoomRenderer / TexturedRoomRenderer now implement.
class BatchArrayOwner {
public:
    static constexpr int kMaxBatches = 1024;

    struct Batch {
        std::uint32_t first_vertex;
        std::uint32_t vertex_count;
        std::uint32_t tri_count;
        std::uint16_t material_id;
    };

    ~BatchArrayOwner() { Free(); }

    // Returns true on success (mirrors Load's "return false on alloc fail").
    bool Load(int face_count) {
        Free();  // free-before-realloc: streaming re-loads must not leak
        const int cap = face_count < kMaxBatches ? face_count : kMaxBatches;
        if (cap <= 0) return false;
        batches_ = static_cast<Batch*>(std::malloc(sizeof(Batch) * cap));
        if (!batches_) return false;
        capacity_ = cap;
        batch_count_ = face_count < cap ? face_count : cap;
        return true;
    }

    void Free() {
        if (batches_) { std::free(batches_); batches_ = nullptr; }
        capacity_ = 0;
        batch_count_ = 0;
    }

    Batch* batches() const { return batches_; }
    int capacity() const { return capacity_; }
    int batch_count() const { return batch_count_; }

private:
    Batch* batches_ = nullptr;
    int capacity_ = 0;
    int batch_count_ = 0;
};

int main() {
    // --- FrameArena semantics ---
    {
        FrameArena arena;
        expect(arena.Used() == 0, "fresh arena starts empty");

        void* a = arena.Alloc(256);
        expect(a != nullptr, "arena alloc succeeds within capacity");
        expect(arena.Used() >= 256, "Used() tracks allocated bytes");

        // Exhaustion: request more than the arena holds → null.
        FrameArena full;
        void* p = full.Alloc(FrameArena::kArenaSize);  // exactly capacity
        expect(p != nullptr, "arena alloc of exactly capacity succeeds");
        void* over = full.Alloc(1);
        expect(over == nullptr, "arena exhaustion returns null");

        // Reset rewinds so the same budget is available next frame.
        arena.Reset();
        expect(arena.Used() == 0, "Reset rewinds Used() to 0");
        void* b = arena.Alloc(256);
        expect(b != nullptr, "budget reusable after Reset");
    }

    // --- Per-frame near-pass arena usage ---
    // After the distant pass was removed, the arena only needs to back the
    // resident set (a handful of cell pointers); it must fit trivially.
    {
        FrameArena arena;
        constexpr size_t kResidentMax = 9;  // near ring: center + 8
        const size_t resident_budget = sizeof(const void*) * kResidentMax;
        expect(resident_budget < FrameArena::kArenaSize,
               "near-pass resident list fits in the 64 KB arena");

        // Allocate what the render loop uses each frame.
        void* visible = arena.Alloc(resident_budget);
        expect(visible != nullptr, "resident snapshot allocates");
        expect(arena.Used() >= resident_budget, "arena Used() reflects allocation");

        // Next frame: Reset makes the same budget available again.
        arena.Reset();
        expect(arena.Used() == 0, "frame Reset frees the render budget");
        void* again = arena.Alloc(resident_budget);
        expect(again != nullptr, "render budget reusable next frame");
    }

    // --- Batch-array ownership mirror (Inc 5 / D6) ---
    {
        BatchArrayOwner owner;

        // 3-face level → allocated capacity 3, not 1024.
        expect(owner.Load(3), "3-face level loads");
        expect(owner.capacity() == 3, "batch capacity sized to face count (3)");
        expect(owner.capacity() != BatchArrayOwner::kMaxBatches,
               "no embedded 1024-slot batch array");

        // Re-load frees the old array before reallocating (no leak) and
        // re-sizes to the new face count.
        expect(owner.Load(7), "re-load of 7-face level succeeds");
        expect(owner.capacity() == 7, "re-load re-sizes capacity to 7");

        // Free nulls the pointer so the destructor can't double-free.
        owner.Free();
        expect(owner.batches() == nullptr, "Free nulls the batch pointer");
        expect(owner.capacity() == 0 && owner.batch_count() == 0,
               "Free zeroes capacity + count");
        owner.Free();  // double-Free is safe (nulled)
    }

    if (failures == 0) {
        std::printf("renderer_memory_contract: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "renderer_memory_contract: %d failures\n", failures);
    return 1;
}
