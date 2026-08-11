#pragma once

#include "gameplay/math_types.hpp"

namespace madeline_cube {

// Pure two-pass camera derivation math, shared between the N64 renderer and
// host tests. Mirrors `arch.md` §5 exactly:
//   near_cam    : normal gameplay clip planes.
//   distant_cam : near = near_cam.far * 0.25f * lod_scale;
//                 far  = tile_size * 1.4f.
// `lod_scale` is a coordinate packing scale (analogous to kPosScale) used for
// compressed distant vertices in Inc 4 — it is NOT a clip-plane multiplier.
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

// Build the distant-pass camera from the near camera per `arch.md` §5:
//   distant.near = near.far * 0.25f * lod_scale
//   distant.far  = tile_size * 1.4f
// The distant camera shares the near camera's position/target/up orientation.
// Returns a camera with `far <= near` (invalid) if the inputs produce an
// empty range — the caller must clamp/reject (distant pass would be empty).
inline CameraDesc MakeDistantCamera(const CameraDesc& near,
                                    float tile_size, float lod_scale) {
    CameraDesc c;
    c.fov_deg = near.fov_deg;
    c.near = near.far * 0.25f * lod_scale;
    c.far = tile_size * 1.4f;
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
