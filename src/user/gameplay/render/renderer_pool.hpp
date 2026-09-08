// memory-opt Inc 4: fixed-capacity pool for per-cell renderer objects and
// their per-cell buffers. Replaces `new TexturedRoomRenderer()` /
// `new LvlRoomRenderer()` and the per-cell `malloc`s in the renderer load
// paths so that, after boot, cell transitions allocate NOTHING from the heap
// (the [memory] used bucket stays flat — no drift over a full autowalk).
//
// Design:
//   - Object storage: `Capacity` objects placement-new'd into a byte array
//     (indices stable across resident-set compaction). Release() runs the
//     destructor (the renderer destructors call Free()).
//   - Per-slot buffer blocks: each slot owns `SlotBytes` of 16-aligned
//     storage that the renderer's PERSISTENT per-cell buffers (batches,
//     runs, run_faces, material groups) are carved from. The block is
//     reused by the next occupant; never freed.
//   - Shared parse scratch: one `ScratchBytes` block per pool. Cell parses
//     are serialized (the pump parses one per frame; SetCenter loads
//     synchronously one at a time), so the transient parse buffers (file
//     buf, faces, lvl_verts, specs, order, sorted_specs) share ONE block.
//     Bump-allocated; ResetScratch() at the start of each parse.
//
// Host-safe: no N64 types. The host smoke test instantiates it with a dummy
// type.
//
// Overflow (a cell exceeding its slot budget or the scratch) is an assert +
// loud load failure — never a partial load (the plan's Inc 4 error path).
#ifndef MADELINE_CUBE_RENDERER_POOL_HPP
#define MADELINE_CUBE_RENDERER_POOL_HPP

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <new>

namespace madeline_cube {

// Index into a pool. -1 = none (a resident slot with no renderer yet).
using PoolIndex = int;

template <typename T, size_t Capacity, size_t SlotBytes, size_t ScratchBytes>
class RendererPool {
public:
    static constexpr size_t kCapacity = Capacity;
    static constexpr size_t kSlotBytes = SlotBytes;
    static constexpr size_t kScratchBytes = ScratchBytes;

    RendererPool() = default;
    RendererPool(const RendererPool&) = delete;
    RendererPool& operator=(const RendererPool&) = delete;

    ~RendererPool() {
        for (size_t i = 0; i < kCapacity; ++i) {
            if (in_use_[i]) Release(static_cast<PoolIndex>(i));
        }
    }

    // Placement-new a T into a free slot. Returns nullptr (and leaves
    // *out_index = -1) when the pool is full — capacity = kMaxResident + 1
    // makes that a programming error, so callers assert.
    T* Acquire(PoolIndex* out_index) {
        for (size_t i = 0; i < kCapacity; ++i) {
            if (!in_use_[i]) {
                in_use_[i] = true;
                if (i + 1 > high_water_) high_water_ = i + 1;
                T* obj = new (&objects_[i * sizeof(T)]) T();
                *out_index = static_cast<PoolIndex>(i);
                return obj;
            }
        }
        *out_index = -1;
        return nullptr;
    }

    // Run the destructor and free the slot. The slot buffer block is NOT
    // cleared — the next occupant's AttachSlotBuffer resets its own cursor.
    void Release(PoolIndex index) {
        assert(index >= 0 && static_cast<size_t>(index) < kCapacity);
        assert(in_use_[static_cast<size_t>(index)]);
        T* obj = At(index);
        obj->~T();
        in_use_[static_cast<size_t>(index)] = false;
    }

    T* At(PoolIndex index) {
        assert(index >= 0 && static_cast<size_t>(index) < kCapacity);
        return reinterpret_cast<T*>(&objects_[static_cast<size_t>(index) * sizeof(T)]);
    }

    bool InUse(PoolIndex index) const {
        assert(index >= 0 && static_cast<size_t>(index) < kCapacity);
        return in_use_[static_cast<size_t>(index)];
    }

    // High-water mark of concurrently-live objects since construction.
    size_t PeakHighWater() const { return high_water_; }

    // Pointer into the slot's preallocated buffer block (16-aligned). The
    // renderer carves its persistent per-cell buffers from here. `need` must
    // fit — overflow is an assert (a cell exceeding its budget fails loudly).
    uint8_t* SlotBuffer(PoolIndex index, size_t need) {
        assert(index >= 0 && static_cast<size_t>(index) < kCapacity);
        assert(need <= kSlotBytes);
        return &slot_buffers_[static_cast<size_t>(index)][0];
    }

    // Base pointer of the shared parse scratch (16-aligned). The renderer
    // owns its bump cursor (ScratchAlloc) — the pool just hands out the
    // block. ResetScratch is not needed: each renderer's AttachSlotBuffer
    // resets its own cursor, and parses are serialized.
    uint8_t* ScratchPtr() { return &scratch_[0]; }

private:
    // Object storage (16-aligned so T's alignment is satisfied for the
    // renderer types, which contain T3D* pointers).
    alignas(16) uint8_t objects_[kCapacity * sizeof(T)];
    bool in_use_[kCapacity] = {};
    size_t high_water_ = 0;
    // Per-slot persistent buffer blocks.
    alignas(16) uint8_t slot_buffers_[kCapacity][kSlotBytes];
    // Shared transient parse scratch.
    alignas(16) uint8_t scratch_[kScratchBytes];
};

}  // namespace madeline_cube

#endif  // MADELINE_CUBE_RENDERER_POOL_HPP
