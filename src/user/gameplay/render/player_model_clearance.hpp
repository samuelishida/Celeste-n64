#pragma once

#include <algorithm>
#include <cmath>

#include "gameplay/math_types.hpp"

namespace madeline_cube {

// Bounds measured from build/n64-prepared/player.glb at manifest base-scale
// and runtime model scale. static_player_t3dm_smoke.py checks they still match
// the prepared source asset.
constexpr float kMadelineModelRadius = 8.61f;
constexpr float kMadelineModelHeight = 21.86f;
constexpr float kMadelineModelWallGap = 0.5f;

// Conservative lower bound for a feet-pivoted player mesh against a wall
// plane. Mesh is bounded by a horizontal radius and a [0, height] Y interval.
inline float PlayerModelMinimumPlaneGap(
    const Vec3& wall_normal,
    float feet_signed_distance,
    float model_radius,
    float model_height,
    float horizontal_offset
) {
    const float normal_length = std::sqrt(
        wall_normal.x * wall_normal.x + wall_normal.y * wall_normal.y +
        wall_normal.z * wall_normal.z);
    if (normal_length <= 1.0e-5f) return -INFINITY;

    const float nx = wall_normal.x / normal_length;
    const float ny = wall_normal.y / normal_length;
    const float nz = wall_normal.z / normal_length;
    const float horizontal_length = std::sqrt(nx * nx + nz * nz);
    const float vertical_support = ny < 0.0f ? ny * model_height : 0.0f;
    const float radial_support = model_radius * horizontal_length;
    return feet_signed_distance + horizontal_offset * horizontal_length +
           vertical_support - radial_support;
}

inline float PlayerModelMinimumPlaneGapAfterTranslation(
    const Vec3& wall_normal,
    float feet_signed_distance,
    float model_radius,
    float model_height,
    const Vec3& horizontal_translation
) {
    const float normal_length = std::sqrt(
        wall_normal.x * wall_normal.x + wall_normal.y * wall_normal.y +
        wall_normal.z * wall_normal.z);
    if (normal_length <= 1.0e-5f) return -INFINITY;
    const float nx = wall_normal.x / normal_length;
    const float nz = wall_normal.z / normal_length;
    const float horizontal_length = std::sqrt(nx * nx + nz * nz);
    if (horizontal_length <= 1.0e-5f) {
        return PlayerModelMinimumPlaneGap(
            wall_normal, feet_signed_distance, model_radius, model_height, 0.0f);
    }
    const float offset_along_normal =
        (horizontal_translation.x * nx + horizontal_translation.z * nz) /
        horizontal_length;
    return PlayerModelMinimumPlaneGap(
        wall_normal, feet_signed_distance, model_radius, model_height,
        offset_along_normal);
}

// Minimum translation along the horizontal wall normal that keeps the full
// bounded mesh at least minimum_gap in front of the wall. This matters on
// overhangs: top of model leans into wall even when feet/capsule remain clear.
inline float PlayerModelHorizontalOffset(
    const Vec3& wall_normal,
    float feet_signed_distance,
    float model_radius,
    float model_height,
    float minimum_gap
) {
    const float normal_length = std::sqrt(
        wall_normal.x * wall_normal.x + wall_normal.y * wall_normal.y +
        wall_normal.z * wall_normal.z);
    if (normal_length <= 1.0e-5f) return 0.0f;

    const float nx = wall_normal.x / normal_length;
    const float ny = wall_normal.y / normal_length;
    const float nz = wall_normal.z / normal_length;
    const float horizontal_length = std::sqrt(nx * nx + nz * nz);
    if (horizontal_length <= 1.0e-5f) return 0.0f;

    const float vertical_support = ny < 0.0f ? ny * model_height : 0.0f;
    const float radial_support = model_radius * horizontal_length;
    const float required_gap = minimum_gap - feet_signed_distance -
                               vertical_support + radial_support;
    return required_gap > 0.0f ? required_gap / horizontal_length : 0.0f;
}

// Find the shortest horizontal translation that clears the model from both
// wall planes during an outer-corner ease. The body interpolates between two
// wall anchors while its active wall normal has already changed, so checking
// only the new face lets the model pass through the old face.
inline Vec3 PlayerModelCornerHorizontalOffset(
    const Vec3& normal_a,
    float feet_gap_a,
    const Vec3& normal_b,
    float feet_gap_b,
    float model_radius,
    float model_height,
    float minimum_gap
) {
    const auto plane_constraint = [&](const Vec3& normal, float feet_gap,
                                      float* hx, float* hz) {
        const float normal_length = std::sqrt(
            normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
        if (normal_length <= 1.0e-5f) return 0.0f;
        const float nx = normal.x / normal_length;
        const float nz = normal.z / normal_length;
        const float horizontal_length = std::sqrt(nx * nx + nz * nz);
        if (horizontal_length <= 1.0e-5f) return 0.0f;
        *hx = nx / horizontal_length;
        *hz = nz / horizontal_length;
        const float current_gap = PlayerModelMinimumPlaneGap(
            normal, feet_gap, model_radius, model_height, 0.0f);
        return std::max(0.0f, minimum_gap - current_gap) / horizontal_length;
    };

    float hax = 0.0f, haz = 0.0f;
    float hbx = 0.0f, hbz = 0.0f;
    const float required_a = plane_constraint(normal_a, feet_gap_a, &hax, &haz);
    const float required_b = plane_constraint(normal_b, feet_gap_b, &hbx, &hbz);
    const float alignment = hax * hbx + haz * hbz;
    const float determinant = 1.0f - alignment * alignment;

    float best_x = 0.0f;
    float best_z = 0.0f;
    float best_length_sq = INFINITY;
    bool found = false;
    const auto consider = [&](float x, float z) {
        if (hax * x + haz * z < required_a - 1.0e-4f ||
            hbx * x + hbz * z < required_b - 1.0e-4f) {
            return;
        }
        const float length_sq = x * x + z * z;
        if (length_sq < best_length_sq) {
            best_x = x;
            best_z = z;
            best_length_sq = length_sq;
            found = true;
        }
    };

    consider(0.0f, 0.0f);
    consider(hax * required_a, haz * required_a);
    consider(hbx * required_b, hbz * required_b);
    if (determinant > 1.0e-4f) {
        const float amount_a = (required_a - alignment * required_b) / determinant;
        const float amount_b = (required_b - alignment * required_a) / determinant;
        consider(hax * amount_a + hbx * amount_b,
                 haz * amount_a + hbz * amount_b);
    }

    // Opposing coplanar constraints can be infeasible from a horizontal
    // translation alone. Prefer satisfying the tighter face rather than
    // silently returning no clearance adjustment.
    if (!found) {
        if (required_a >= required_b) {
            best_x = hax * required_a;
            best_z = haz * required_a;
        } else {
            best_x = hbx * required_b;
            best_z = hbz * required_b;
        }
    }
    return {best_x, 0.0f, best_z};
}

}  // namespace madeline_cube
