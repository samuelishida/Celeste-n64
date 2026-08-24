#pragma once

#include "gameplay/math_types.hpp"

namespace madeline_cube {

// Single near-pass camera derivation math, shared between the N64 renderer
// and host tests. The default far=800 covers the 9-cell resident ring; the
// render-distance Inc 5 distant tier widens it to 2000 at build time when
// kEnableDistantTier is ON.
struct CameraDesc {
    float fov_deg = 45.0f;
    float near = 5.0f;  // matches near-pass clip plane
    float far = 800.0f;  // 2000 when kEnableDistantTier (Inc 5)
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
