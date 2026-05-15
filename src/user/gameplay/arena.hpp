#pragma once

#include <cstddef>
#include <cstdint>

namespace madeline_cube {

// Simple bump allocator for transient per-frame or per-scoped work.
// No individual free; Reset() frees everything at once.
class Arena {
public:
    Arena() = default;
    Arena(uint8_t* base, size_t capacity);

    // Allocate `bytes` from the arena, aligned to `align`.
    // Returns nullptr if the arena is exhausted.
    void* Alloc(size_t bytes, size_t align = 8);

    // Reset all allocations. O(1).
    void Reset();

    // Bytes currently allocated.
    size_t Used() const { return used_; }

    // Bytes remaining.
    size_t Available() const { return capacity_ > used_ ? (capacity_ - used_) : 0; }

    // Total capacity.
    size_t Capacity() const { return capacity_; }

private:
    uint8_t* base_ = nullptr;
    size_t capacity_ = 0;
    size_t used_ = 0;
};

}  // namespace madeline_cube
