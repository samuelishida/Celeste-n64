#pragma once

#include <cstddef>
#include <cstdint>

namespace n64 {

// A simple frame-scoped arena allocator (arch.md §30). Owns a fixed-size
// buffer; `Alloc` bumps a pointer (no free); `Reset` rewinds to the start at
// the beginning of each frame. Used for transient per-frame allocations
// (visible tile lists, distant sort arrays, matrix scratch). Host-safe — no
// N64 types.
class FrameArena {
public:
    static constexpr size_t kArenaSize = 64 * 1024;  // 64 KB

    FrameArena() = default;
    FrameArena(const FrameArena&) = delete;
    FrameArena& operator=(const FrameArena&) = delete;

    // Allocate `size` bytes, aligned to `align`. Returns nullptr if the arena
    // is exhausted (a budget violation — the caller must assert).
    void* Alloc(size_t size, size_t align = 8) {
        const uintptr_t base = reinterpret_cast<uintptr_t>(buffer_ + offset_);
        const uintptr_t aligned = (base + align - 1) & ~(align - 1);
        const size_t new_offset = static_cast<size_t>(aligned - reinterpret_cast<uintptr_t>(buffer_)) + size;
        if (new_offset > kArenaSize) return nullptr;
        offset_ = new_offset;
        return reinterpret_cast<void*>(aligned);
    }

    // Rewind to the start (call at the beginning of each frame).
    void Reset() { offset_ = 0; }

    // Bytes used so far this frame.
    size_t Used() const { return offset_; }
    // Bytes remaining this frame.
    size_t Remaining() const { return kArenaSize - offset_; }

private:
    alignas(8) uint8_t buffer_[kArenaSize];
    size_t offset_ = 0;
};

}  // namespace n64
