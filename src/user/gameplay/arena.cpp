#include "arena.hpp"

namespace madeline_cube {

Arena::Arena(uint8_t* base, size_t capacity)
    : base_(base), capacity_(capacity), used_(0) {}

void* Arena::Alloc(size_t bytes, size_t align) {
    if (base_ == nullptr || capacity_ == 0) {
        return nullptr;
    }

    const uintptr_t current = reinterpret_cast<uintptr_t>(base_ + used_);
    const size_t padding = (align - (current % align)) % align;
    const size_t next_used = used_ + padding + bytes;

    if (next_used > capacity_) {
        return nullptr;
    }

    used_ = next_used;
    return base_ + (used_ - bytes);
}

void Arena::Reset() {
    used_ = 0;
}

}  // namespace madeline_cube
