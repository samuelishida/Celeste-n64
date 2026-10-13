#pragma once

#include <array>
#include <cstdint>

#include <libdragon.h>
#include <t3d/t3d.h>

#include "gameplay/math_types.hpp"
#include "gameplay/world/mappack_loader.hpp"

namespace madeline_cube {

class DynamicSolidGeometry;
class MaterialCatalog;
struct CameraDesc;
struct RenderCounters;

// Renders DSOL actor-local source faces with each actor's live transform.
// Packed vertices are cached because only actor matrices change during play.
class DynamicSolidRenderer {
public:
    DynamicSolidRenderer() = default;
    ~DynamicSolidRenderer() { Free(); }
    DynamicSolidRenderer(const DynamicSolidRenderer&) = delete;
    DynamicSolidRenderer& operator=(const DynamicSolidRenderer&) = delete;

    bool Prepare(const DynamicSolidGeometry& geometry);
    void Draw(const DynamicSolidGeometry& geometry,
              const MaterialCatalog* materials, const CameraDesc& camera,
              RenderCounters* counters,
              const Vec3* player_position = nullptr);
    void Free();

private:
    T3DVertPacked* vertices_ = nullptr;
    // Tiny3D queues matrix DMA asynchronously. Keep one uncached matrix per
    // actor so the next actor cannot overwrite a matrix still consumed by RSP.
    T3DMat4FP* actor_matrices_ = nullptr;
    uint16_t actor_count_ = 0;
    uint32_t vertex_count_ = 0;
    const DynamicSolidGeometry* geometry_ = nullptr;
    std::array<uint8_t, MapSpecV2::kMaxDynamicSolids> was_visible_{};
    std::array<uint8_t, MapSpecV2::kMaxDynamicSolids> visibility_known_{};
};

}  // namespace madeline_cube
