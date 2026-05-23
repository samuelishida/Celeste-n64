#pragma once

#include <libdragon.h>
#include <t3d/t3d.h>
#include <t3d/t3dmath.h>

#include "gameplay/world/world.hpp"

namespace madeline_cube {

class TrafficBlockRenderer {
public:
    bool Init();
    void UpdateInstance(int idx, const Vec3& center, const Vec3& he);
    void Draw() const;
    void Free();

private:
    static constexpr int kMaxInstances = Room::kMaxTraffic;
    struct PackedVertex {
        int16_t posA[3];
        uint16_t normA;
        int16_t posB[3];
        uint16_t normB;
        uint32_t rgbaA;
        uint32_t rgbaB;
    };
    PackedVertex* verts_ = nullptr;
    uint16_t indices_[36] = {};
    T3DMat4FP* matrices_[kMaxInstances] = {};
    int instance_count_ = 0;
    sprite_t* material_sprite_ = nullptr;
};

}  // namespace madeline_cube
