#pragma once

#include "gameplay/math_types.hpp"
#include "gameplay/render/lod_math.hpp"  // MapFarClipDistance, AABB

namespace madeline_cube {

// Pure two-pass camera derivation math, shared between the N64 renderer and
// host tests. Mirrors `arch.md` §5:
//   near_cam    : normal gameplay clip planes.
//   distant_cam : near = just past the resident ring (world-space);
//                 far  = full map diagonal (world-space).
// `lod_scale` is a coordinate packing scale (analogous to kPosScale) used for
// compressed distant vertices — it is NOT a clip-plane multiplier and no
// longer affects the distant near plane (retained in the signature for future
// compressed-coordinate projection work).
// This header uses only `Vec3` and `float` — no N64 types.

struct CameraDesc {
    float fov_deg = 45.0f;
    float near = 20.0f;
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

// Build the distant-pass camera with world-space cull distances:
//   distant.near = tile_size * near_margin   (just past the resident ring)
//   distant.far  = MapFarClipDistance(world_bounds, far_margin)  (full map)
// The distant camera shares the near camera's position/target/up orientation.
// `world_bounds` is the union of all room AABBs; if null or zero-extent, `far`
// falls back to `tile_size * 16.0f`. Returns a camera with `far <= near`
// (invalid) if the inputs produce an empty range — the caller must
// clamp/reject (distant pass would be empty).
inline CameraDesc MakeDistantCamera(const CameraDesc& near,
                                    float tile_size, float lod_scale,
                                    const AABB* world_bounds,
                                    float near_margin = 1.5f,
                                    float far_margin = 1.15f) {
    (void)lod_scale;  // retained for future compressed-coordinate projection
    CameraDesc c;
    c.fov_deg = near.fov_deg;
    c.near = tile_size * near_margin;  // world-space ring edge
    c.far = MapFarClipDistance(world_bounds, far_margin);
    if (c.far <= 0.0f) c.far = tile_size * 16.0f;  // fallback (null/zero bounds)
    c.pos = near.pos;
    c.target = near.target;
    c.up = near.up;
    return c;
}

// Returns true if the distant camera has a valid, non-empty range.
inline bool ValidateDistantCamera(const CameraDesc& distant) {
    return distant.far > distant.near && distant.near > 0.0f;
}

}  // namespace madeline_cube
