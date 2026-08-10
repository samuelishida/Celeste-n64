#pragma once

#include <cmath>

#include "gameplay/math_types.hpp"

namespace madeline_cube {

// Pure transform math shared between the N64 renderer and host tests.
//
// `LvlRoomRenderer::Load` subtracts `render_origin` from each world-space
// vertex and packs the result into int16 fixed-point at `kPosScale`:
//     packed = (world - origin) * kPosScale
// The model matrix then applies scale `kInvScale` (= 1/kPosScale) followed by
// translation `origin`, so the drawn world position is:
//     drawn = kInvScale * packed + origin
//           = kInvScale * (world - origin) * kPosScale + origin
//           = (world - origin) + origin
//           = world
// This helper validates that identity so the visual frame provably matches
// the collision frame (which is world-space global). It uses only `Vec3` and
// `float` — no N64 types — so it compiles on host and device alike.

// Reconstruct the drawn world position from a source world position and the
// render-origin rebase. Returns the position the renderer would draw.
inline Vec3 ReconstructDrawnWorld(const Vec3& world, const Vec3& origin,
                                  float kPosScale, float kInvScale) {
    Vec3 packed;
    packed.x = (world.x - origin.x) * kPosScale;
    packed.y = (world.y - origin.y) * kPosScale;
    packed.z = (world.z - origin.z) * kPosScale;

    Vec3 drawn;
    drawn.x = kInvScale * packed.x + origin.x;
    drawn.y = kInvScale * packed.y + origin.y;
    drawn.z = kInvScale * packed.z + origin.z;
    return drawn;
}

// Returns true if the render-origin rebase round-trips `world` back to itself
// within `eps` (i.e. the drawn frame matches the collision frame).
inline bool ValidateRenderOriginTransform(const Vec3& world, const Vec3& origin,
                                          float kPosScale, float kInvScale,
                                          float eps = 1e-3f) {
    const Vec3 drawn = ReconstructDrawnWorld(world, origin, kPosScale, kInvScale);
    return std::fabs(drawn.x - world.x) < eps &&
           std::fabs(drawn.y - world.y) < eps &&
           std::fabs(drawn.z - world.z) < eps;
}

}  // namespace madeline_cube
