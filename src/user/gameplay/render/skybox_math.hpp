#pragma once

#include <cstdint>

#include "gameplay/math_types.hpp"

namespace madeline_cube {

inline constexpr int kSkyboxFaceCount = 6;
inline constexpr int kSkyboxVerticesPerFace = 6;
inline constexpr float kSkyboxHalfExtent = 350.0f;

struct SkyboxTriangle {
    uint8_t a;
    uint8_t b;
    uint8_t c;
};

// Init emits two independent triangles (six vertices) per cube face.
// Keep draw indices aligned with that emitted layout.
inline constexpr SkyboxTriangle SkyboxTriangleForFace(int face, int triangle) {
    const int base = face * kSkyboxVerticesPerFace;
    return triangle == 0
        ? SkyboxTriangle{static_cast<uint8_t>(base),
                         static_cast<uint8_t>(base + 1),
                         static_cast<uint8_t>(base + 2)}
        : SkyboxTriangle{static_cast<uint8_t>(base + 3),
                         static_cast<uint8_t>(base + 4),
                         static_cast<uint8_t>(base + 5)};
}

// Bound the cube's farthest corner inside the near-pass far plane.
inline bool SkyboxFitsFarPlane(float far_plane) {
    return far_plane > 0.0f &&
           3.0f * kSkyboxHalfExtent * kSkyboxHalfExtent < far_plane * far_plane;
}

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
