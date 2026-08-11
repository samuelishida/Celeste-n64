#pragma once

#include <cmath>

#include "gameplay/math_types.hpp"

namespace madeline_cube {

// Pure camera-relative transform math shared between the N64 renderer and
// host tests.
//
// The near pass keeps its vertices packed against their fixed per-cell render
// origin (see `LvlRoomRenderer`), then shifts the model-matrix translation to
// be relative to the camera:
//     drawn = (world - origin) * kPosScale * kInvScale + (origin - camera)
//           = (world - origin) + (origin - camera)
//           = world - camera
// So the visible world is expressed relative to the camera, keeping the
// coordinates numerically small and avoiding the depth non-linearity of a
// huge world-space far plane.
//
// This header uses only `Vec3` and `float` — no N64 types — so it compiles
// on host and device alike.

// Express a world-space position relative to the camera (camera becomes the
// origin). Equivalent to `world - camera_pos`.
inline Vec3 ToCameraSpace(const Vec3& world, const Vec3& camera_pos) {
    Vec3 local;
    local.x = world.x - camera_pos.x;
    local.y = world.y - camera_pos.y;
    local.z = world.z - camera_pos.z;
    return local;
}

// Inverse of `ToCameraSpace`. Reconstructs the world-space position from a
// camera-relative local position. Equivalent to `local + camera_pos`.
inline Vec3 FromCameraSpace(const Vec3& local, const Vec3& camera_pos) {
    Vec3 world;
    world.x = local.x + camera_pos.x;
    world.y = local.y + camera_pos.y;
    world.z = local.z + camera_pos.z;
    return world;
}

// Returns true if a world position round-trips exactly through
// camera-space (ToCameraSpace then FromCameraSpace) back to itself within
// `eps`. Used by host tests to prove the transform is lossless.
inline bool ValidateCameraSpaceRoundTrip(const Vec3& world,
                                         const Vec3& camera_pos,
                                         float eps = 1e-3f) {
    const Vec3 local = ToCameraSpace(world, camera_pos);
    const Vec3 round = FromCameraSpace(local, camera_pos);
    return std::fabs(round.x - world.x) < eps &&
           std::fabs(round.y - world.y) < eps &&
           std::fabs(round.z - world.z) < eps;
}

// Returns true if the camera-relative delta (world - camera_pos), when scaled
// by `kPosScale`, stays inside the int16 range (±32767) on every axis. This
// is the overflow guard for fixed-point vertex packing: packed = (world -
// camera) * kPosScale must fit int16.
inline bool PackedFitsInt16(const Vec3& world, const Vec3& camera_pos,
                            float kPosScale) {
    const Vec3 local = ToCameraSpace(world, camera_pos);
    const float limit = 32767.0f;
    return std::fabs(local.x * kPosScale) <= limit &&
           std::fabs(local.y * kPosScale) <= limit &&
           std::fabs(local.z * kPosScale) <= limit;
}

}  // namespace madeline_cube
