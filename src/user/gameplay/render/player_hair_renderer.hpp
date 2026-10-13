#pragma once

#include <cstdint>

#include <t3d/t3d.h>

#include "gameplay/math_types.hpp"
#include "gameplay/render/player_hair_geometry.hpp"
#include "gameplay/render/player_hair_simulation.hpp"
#include "gameplay/render/player_hair_sphere.hpp"

namespace madeline_cube {

class PlayerHairRenderer {
public:
    bool Init();
    void Free();
    bool Draw(const PlayerHairSimulation& hair, const Vec3& forward_world,
              const Vec3& squish_og, color_t color_rgba,
              float roundness = 0.0f);
    bool IsReady() const { return buffer_count_ > 0; }

private:
    static constexpr int kSphereVertexCount = PlayerHairSphere::kVertexCount;
    static constexpr int kSpherePairCount = kSphereVertexCount / 2;
    static constexpr int kSphereIndexCount = PlayerHairSphere::kIndexCount;
    static constexpr int kMaxFrameBuffers = 3;
    static constexpr int kVerticesPerFrame = PlayerHairSimulation::kMaxNodes * kSphereVertexCount;

    bool BuildNodeVertices(T3DVertPacked* destination, const PlayerHairSimulation& hair,
                          int node_index, const PlayerHairRotation& rotation,
                          const Vec3& squish_og, float roundness) const;

    PlayerHairSphere sphere_{};
    T3DVertPacked* frame_buffers_[kMaxFrameBuffers]{};
    // Uncached: the RSP DMAs the matrix straight from its physical address.
    T3DMat4FP* identity_matrix_ = nullptr;
    uint8_t buffer_count_ = 0;
    uint8_t buffer_index_ = 0;
};

}  // namespace madeline_cube
