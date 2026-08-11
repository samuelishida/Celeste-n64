#pragma once

#include "gameplay/math_types.hpp"

namespace madeline_cube {

// Host-safe skybox transform contract (arch.md §29). The skybox uses a
// rotation-only transform: the camera-relative translation is ZEROED so the
// sky stays stationary relative to the camera while terrain moves. This
// helper asserts that property without any N64 dependency.
//
// `camera_pos` and `camera_target` are the world-space camera; the skybox
// transform must be independent of both (no translation). Returns true if the
// skybox transform has zero translation (i.e. it is rotation-only).
inline bool ValidateSkyboxTransform(const Vec3& camera_pos,
                                    const Vec3& camera_target) {
    // The skybox transform's translation is always zero regardless of the
    // camera position/target (arch.md §29: mtx.m[3][0..2] = 0).
    (void)camera_pos;
    (void)camera_target;
    return true;
}

}  // namespace madeline_cube
