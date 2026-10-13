#pragma once

#include <cmath>

#include "gameplay/math_types.hpp"

namespace madeline_cube {

// OG World snapshots Camera.Frustum.GetBoundingBox().Inflate(10) once before
// either actor pass. Source Game renders to a fixed 640x360 target (16:9);
// Camera's vertical FOV starts at 45 degrees and scales by FOVMultiplier.
// Keep the box independent of renderer/T3D types so scene scheduling and host
// fixtures use the same geometry.
struct ActorUpdateView {
    Vec3 min;
    Vec3 max;
    bool valid = false;

    bool IntersectsBounds(const Vec3& bounds_min,
                          const Vec3& bounds_max) const {
        if (!valid) return true;  // malformed camera must fail open
        return bounds_min.x <= max.x && bounds_max.x >= min.x &&
               bounds_min.y <= max.y && bounds_max.y >= min.y &&
               bounds_min.z <= max.z && bounds_max.z >= min.z;
    }
};

inline ActorUpdateView BuildActorUpdateView(const Vec3& camera_position,
                                            const Vec3& camera_target,
                                            float fov_multiplier) {
    constexpr float kPi = 3.14159265358979323846f;
    constexpr float kVerticalFovDegrees = 45.0f;
    constexpr float kAspectRatio = 16.0f / 9.0f;
    constexpr float kNearPlane = 20.0f;  // Celeste64-og World constructor
    constexpr float kFarPlane = 800.0f;
    constexpr float kInflation = 10.0f;  // World.Update broadphase margin

    ActorUpdateView result{};
    const Vec3 delta{camera_target.x - camera_position.x,
                     camera_target.y - camera_position.y,
                     camera_target.z - camera_position.z};
    const float forward_length_squared = delta.x * delta.x +
        delta.y * delta.y + delta.z * delta.z;
    if (!std::isfinite(forward_length_squared) ||
        forward_length_squared <= 1.0e-12f ||
        !std::isfinite(fov_multiplier) || fov_multiplier <= 0.0f) {
        return result;
    }

    const float inverse_forward_length = 1.0f / std::sqrt(forward_length_squared);
    const Vec3 forward{delta.x * inverse_forward_length,
                       delta.y * inverse_forward_length,
                       delta.z * inverse_forward_length};
    // N64 runtime uses +Y up. Symmetric corner signs make the right-vector
    // handedness irrelevant to the resulting axis-aligned bound.
    const Vec3 right{forward.z, 0.0f, -forward.x};
    const float right_length_squared = right.x * right.x + right.z * right.z;
    if (!std::isfinite(right_length_squared) ||
        right_length_squared <= 1.0e-12f) {
        return result;
    }
    const float inverse_right_length = 1.0f / std::sqrt(right_length_squared);
    const Vec3 normalized_right{right.x * inverse_right_length, 0.0f,
                                right.z * inverse_right_length};
    const Vec3 camera_up{
        forward.y * normalized_right.z - forward.z * normalized_right.y,
        forward.z * normalized_right.x - forward.x * normalized_right.z,
        forward.x * normalized_right.y - forward.y * normalized_right.x,
    };

    const float half_fov_radians =
        (kVerticalFovDegrees * fov_multiplier * 0.5f) * (kPi / 180.0f);
    const float tangent = std::tan(half_fov_radians);
    if (!std::isfinite(tangent) || tangent <= 0.0f) return result;

    const float depths[2] = {kNearPlane, kFarPlane};
    for (int depth_index = 0; depth_index < 2; ++depth_index) {
        const float depth = depths[depth_index];
        const Vec3 center{camera_position.x + forward.x * depth,
                          camera_position.y + forward.y * depth,
                          camera_position.z + forward.z * depth};
        const float half_height = depth * tangent;
        const float half_width = half_height * kAspectRatio;
        for (int vertical_sign = -1; vertical_sign <= 1; vertical_sign += 2) {
            for (int horizontal_sign = -1; horizontal_sign <= 1;
                 horizontal_sign += 2) {
                const Vec3 corner{
                    center.x + normalized_right.x * half_width * horizontal_sign +
                        camera_up.x * half_height * vertical_sign,
                    center.y + normalized_right.y * half_width * horizontal_sign +
                        camera_up.y * half_height * vertical_sign,
                    center.z + normalized_right.z * half_width * horizontal_sign +
                        camera_up.z * half_height * vertical_sign,
                };
                if (!result.valid) {
                    result.min = result.max = corner;
                    result.valid = true;
                } else {
                    if (corner.x < result.min.x) result.min.x = corner.x;
                    if (corner.y < result.min.y) result.min.y = corner.y;
                    if (corner.z < result.min.z) result.min.z = corner.z;
                    if (corner.x > result.max.x) result.max.x = corner.x;
                    if (corner.y > result.max.y) result.max.y = corner.y;
                    if (corner.z > result.max.z) result.max.z = corner.z;
                }
            }
        }
    }

    result.min.x -= kInflation;
    result.min.y -= kInflation;
    result.min.z -= kInflation;
    result.max.x += kInflation;
    result.max.y += kInflation;
    result.max.z += kInflation;
    return result;
}

}  // namespace madeline_cube
