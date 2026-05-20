#pragma once

#include <cstdint>
#include <sprite.h>

namespace madeline_cube {

class MaterialCatalog {
public:
    bool Load(const char* level_name);
    void Unload();
    sprite_t* MaterialFor(uint16_t material_id) const;
    int Count() const { return count_; }

private:
    static constexpr int kMaxMaterials = 32;
    // Null entries are valid reserved material slots, e.g. invisible TB_empty.
    sprite_t* sprites_[kMaxMaterials] = {};
    int count_ = 0;
};

}  // namespace madeline_cube
