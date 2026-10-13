#pragma once

#include <cstdint>
#include <sprite.h>

namespace madeline_cube {

class MaterialCatalog {
public:
    struct MaterialProfile {
        bool has_texture = false;
        bool alpha_blend = false;
        bool bilinear_filter = false;
        bool repeat_s = true;
        bool repeat_t = true;
        uint8_t uv_scale_u = 1;
        uint8_t uv_scale_v = 1;
    };

    bool Load(const char* level_name);
    void Unload();
    sprite_t* MaterialFor(uint16_t material_id) const;
    const MaterialProfile* ProfileFor(uint16_t material_id) const;
    int Count() const { return count_; }

private:
    static constexpr int kMaxMaterials = 64;
    // Null entries are valid reserved material slots, e.g. invisible TB_empty.
    sprite_t* sprites_[kMaxMaterials] = {};
    MaterialProfile profiles_[kMaxMaterials] = {};
    int count_ = 0;
};

}  // namespace madeline_cube
