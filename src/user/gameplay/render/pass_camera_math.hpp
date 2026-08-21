#pragma once

#include "gameplay/math_types.hpp"

namespace madeline_cube {

// Single near-pass camera derivation math, shared between the N64 renderer
// and host tests. After the z-split distant pass was removed, only one camera
// is needed: normal gameplay clip planes (near=5, far=800) that cover the
// 9-cell resident ring. This header uses only `Vec3` and `float` — no N64
// types.

struct CameraDesc {
    float fov_deg = 45.0f;
    float near = 5.0f;  // Inc 6: lowered from 20.0 (matches near-pass clip plane)
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

}  // namespace madeline_cube
