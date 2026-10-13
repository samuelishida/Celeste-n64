#pragma once

#include "gameplay/math_types.hpp"
#include "gameplay/render/lod_math.hpp"  // MapFarClipDistance, AABB

namespace madeline_cube {

// Two-pass camera derivation shared between the N64 renderer and host tests.
// Near camera covers detailed ready cells; distant camera fills nonresident
// cells and horizon. READY-based ownership prevents overlap. Both share one
// world-space view orientation.
struct CameraDesc {
    float fov_deg = 45.0f;
    float near = 5.0f;
    float far = 800.0f;
    Vec3 pos;     // world-space camera position
    Vec3 target;  // world-space look target
    Vec3 up;      // world-space up
};

// Build the near-pass camera from explicit clip planes.
inline CameraDesc MakeNearCamera(float fov_deg, float near_plane,
                                 float far_plane, const Vec3& camera_pos,
                                 const Vec3& target, const Vec3& up) {
    CameraDesc c;
    c.fov_deg = fov_deg;
    c.near = near_plane;
    c.far = far_plane;
    c.pos = camera_pos;
    c.target = target;
    c.up = up;
    return c;
}

// Build distant-pass clip planes in world units. Near matches the detailed
// pass: cell ownership (READY near vs. pending DLOD) prevents overlap, while
// clipping at the nominal ring radius would slice nonresident cells when the
// camera leads the active cell during fast movement. Far covers map bounds
// with margin. LOD vertex compression scale does not belong in clip distances.
inline CameraDesc MakeDistantCamera(const CameraDesc& near,
                                    float tile_size,
                                    const AABB* world_bounds,
                                    float far_margin = 1.15f) {
    CameraDesc c = near;
    if (tile_size < 1.0f) tile_size = 1.0f;
    c.near = near.near;
    c.far = MapFarClipDistance(world_bounds, far_margin);
    // No valid map bounds: fall back to ~16 cells of horizon. The old
    // `near + tile_size` (~245u) collapsed the distant far to a single cell,
    // clipping the horizon to the resident ring and defeating the z-split.
    if (c.far <= c.near) c.far = tile_size * 16.0f;
    return c;
}

inline bool ValidateDistantCamera(const CameraDesc& distant) {
    return distant.near > 0.0f && distant.far > distant.near;
}

}  // namespace madeline_cube
