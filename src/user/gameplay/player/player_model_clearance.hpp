#pragma once

#include <algorithm>
#include <cmath>

#include "gameplay/math_types.hpp"
#include "gameplay/world/world.hpp"

namespace madeline_cube {

// Bounds measured from build/n64-prepared/player.glb at manifest base-scale
// and runtime model scale. static_player_t3dm_smoke.py checks they still match
// the prepared source asset.
constexpr float kMadelineModelRadius = 10.8375f;
constexpr float kMadelineModelHeight = 15.006325f;
constexpr float kMadelineModelWallGap = 0.5f;
// Source Armature translation [0.1236680299, 0, 0], converted by gltf_to_t3d's
// base scale (64) and the runtime matrix scale (0.07735). Tiny3D's importer
// drops transformed skin ancestors, so restore this actor-space model origin
// offset in the outer matrix after preserving it in the joint bind/clip data.
constexpr float kMadelineModelRootOffsetX = 0.6122062f;

// Packed player.t3dm horizontal vertex bounds before the runtime matrix scale.
// Use these oriented bounds for wall support: the isotropic radius is useful
// for finding nearby faces, but overestimates the mesh depth when Madeline is
// facing a wall (the front support is 82 packed units, not the 140-unit box
// corner radius).
constexpr float kMadelineModelMinX = -81.0f;
constexpr float kMadelineModelMaxX = 97.0f;
constexpr float kMadelineModelMinZ = -100.0f;
constexpr float kMadelineModelMaxZ = 82.0f;

// The model's local +Z follows facing; local +X points to its right.
inline Vec3 PlayerModelRootOffset(const Vec3& facing, float presentation_scale) {
    const float facing_length = std::sqrt(facing.x * facing.x + facing.z * facing.z);
    if (facing_length <= 1.0e-5f) {
        return {kMadelineModelRootOffsetX * presentation_scale, 0.0f, 0.0f};
    }
    const float fx = facing.x / facing_length;
    const float fz = facing.z / facing_length;
    return {
        kMadelineModelRootOffsetX * presentation_scale * fz,
        0.0f,
        -kMadelineModelRootOffsetX * presentation_scale * fx,
    };
}

inline float PlayerModelPlaneHorizontalSupport(
    const Vec3& wall_normal,
    float model_radius,
    const Vec3* facing,
    float model_scale
) {
    const float normal_length = std::sqrt(
        wall_normal.x * wall_normal.x + wall_normal.y * wall_normal.y +
        wall_normal.z * wall_normal.z);
    if (normal_length <= 1.0e-5f) return 0.0f;
    const float nx = wall_normal.x / normal_length;
    const float nz = wall_normal.z / normal_length;
    const float horizontal_length = std::sqrt(nx * nx + nz * nz);
    if (horizontal_length <= 1.0e-5f) return 0.0f;

    if (facing == nullptr || model_scale <= 0.0f) {
        return model_radius * horizontal_length;
    }
    const float facing_length = std::sqrt(facing->x * facing->x + facing->z * facing->z);
    if (facing_length <= 1.0e-5f) return model_radius * horizontal_length;

    // The model's local +Z follows facing; local +X points to its right.
    const float fx = facing->x / facing_length;
    const float fz = facing->z / facing_length;
    const float right_x = fz;
    const float right_z = -fx;
    const float local_nx = (nx * right_x + nz * right_z) / horizontal_length;
    const float local_nz = (nx * fx + nz * fz) / horizontal_length;
    const float min_projection =
        (local_nx >= 0.0f ? kMadelineModelMinX : kMadelineModelMaxX) * local_nx +
        (local_nz >= 0.0f ? kMadelineModelMinZ : kMadelineModelMaxZ) * local_nz;
    return std::max(0.0f, -min_projection * model_scale) * horizontal_length;
}

// Conservative lower bound for a feet-pivoted player mesh against a wall
// plane. Mesh is bounded by a horizontal radius and a [0, height] Y interval.
inline float PlayerModelMinimumPlaneGap(
    const Vec3& wall_normal,
    float feet_signed_distance,
    float model_radius,
    float model_height,
    float horizontal_offset,
    const Vec3* facing = nullptr,
    float model_scale = 1.0f
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
    const float radial_support = PlayerModelPlaneHorizontalSupport(
        wall_normal, model_radius, facing, model_scale);
    return feet_signed_distance + horizontal_offset * horizontal_length +
           vertical_support - radial_support;
}

inline float PlayerModelMinimumPlaneGapAfterTranslation(
    const Vec3& wall_normal,
    float feet_signed_distance,
    float model_radius,
    float model_height,
    const Vec3& horizontal_translation,
    const Vec3* facing = nullptr,
    float model_scale = 1.0f
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
            wall_normal, feet_signed_distance, model_radius, model_height, 0.0f,
            facing, model_scale);
    }
    const float offset_along_normal =
        (horizontal_translation.x * nx + horizontal_translation.z * nz) /
        horizontal_length;
    return PlayerModelMinimumPlaneGap(
        wall_normal, feet_signed_distance, model_radius, model_height,
        offset_along_normal, facing, model_scale);
}

// Minimum translation along the horizontal wall normal that keeps the full
// bounded mesh at least minimum_gap in front of the wall. This matters on
// overhangs: top of model leans into wall even when feet/capsule remain clear.
inline float PlayerModelHorizontalOffset(
    const Vec3& wall_normal,
    float feet_signed_distance,
    float model_radius,
    float model_height,
    float minimum_gap,
    const Vec3* facing = nullptr,
    float model_scale = 1.0f
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
    const float radial_support = PlayerModelPlaneHorizontalSupport(
        wall_normal, model_radius, facing, model_scale);
    const float required_gap = minimum_gap - feet_signed_distance -
                               vertical_support + radial_support;
    return required_gap > 0.0f ? required_gap / horizontal_length : 0.0f;
}

struct PlayerModelPlaneConstraint {
    Vec3 normal;
    float feet_signed_distance = 0.0f;
};

constexpr int kMaxPlayerModelWallConstraints = kMaxWallHits * 4;

// Gather nearby wall planes at four heights across the rendered model. A
// waist-only query misses stepped/overhanging faces that intersect Madeline's
// torso or head, which lets stable climbing look correct in physics telemetry
// while the mesh is visibly inside geometry.
inline int QueryPlayerModelWallConstraints(
    const Room& room,
    const Vec3& feet,
    float model_radius,
    float model_height,
    float waist_offset,
    float minimum_gap,
    PlayerModelPlaneConstraint* out_constraints,
    int max_constraints
) {
    if (out_constraints == nullptr || max_constraints <= 0) return 0;
    const float probe_offsets[4] = {
        0.5f,
        waist_offset,
        model_height * 0.5f,
        model_height,
    };
    int count = 0;
    for (float offset : probe_offsets) {
        const Vec3 probe = {feet.x, feet.y + offset, feet.z};
        WallHit hits[kMaxWallHits];
        const int hit_count = QueryWallsFacePlane(
            room, probe, model_radius + minimum_gap, hits, kMaxWallHits,
            nullptr, false);
        for (int i = 0; i < hit_count && count < max_constraints; ++i) {
            const Vec3 delta = {
                feet.x - hits[i].point.x,
                feet.y - hits[i].point.y,
                feet.z - hits[i].point.z,
            };
            out_constraints[count++] = {
                hits[i].plane_normal,
                hits[i].plane_normal.x * delta.x +
                    hits[i].plane_normal.y * delta.y +
                    hits[i].plane_normal.z * delta.z,
            };
        }
    }
    return count;
}

// Shortest horizontal translation satisfying all nearby wall-plane gaps.
// Candidate minima occur at zero, one active plane, or an intersection of two
// active planes; test those candidates against every collected constraint.
inline Vec3 PlayerModelMultiPlaneHorizontalOffset(
    const PlayerModelPlaneConstraint* constraints,
    int count,
    float model_radius,
    float model_height,
    float minimum_gap,
    const Vec3* facing = nullptr,
    float model_scale = 1.0f
) {
    if (constraints == nullptr || count <= 0) return {};
    count = std::min(count, kMaxPlayerModelWallConstraints);

    float hx[kMaxPlayerModelWallConstraints]{};
    float hz[kMaxPlayerModelWallConstraints]{};
    float required[kMaxPlayerModelWallConstraints]{};
    int active_count = 0;
    for (int i = 0; i < count; ++i) {
        const Vec3& n = constraints[i].normal;
        const float normal_length = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
        if (normal_length <= 1.0e-5f) continue;
        const float nx = n.x / normal_length;
        const float nz = n.z / normal_length;
        const float horizontal_length = std::sqrt(nx * nx + nz * nz);
        if (horizontal_length <= 1.0e-5f) continue;
        hx[active_count] = nx / horizontal_length;
        hz[active_count] = nz / horizontal_length;
        const float current_gap = PlayerModelMinimumPlaneGap(
            n, constraints[i].feet_signed_distance,
            model_radius, model_height, 0.0f, facing, model_scale);
        required[active_count] = std::max(
            0.0f, minimum_gap - current_gap) / horizontal_length;
        ++active_count;
    }
    if (active_count == 0) return {};

    float best_x = 0.0f;
    float best_z = 0.0f;
    float best_length_sq = INFINITY;
    float best_violation = INFINITY;
    bool found_feasible = false;
    const auto consider = [&](float x, float z) {
        const float length_sq = x * x + z * z;
        bool feasible = true;
        float violation = 0.0f;
        for (int i = 0; i < active_count; ++i) {
            const float shortfall = std::max(
                0.0f, required[i] - (hx[i] * x + hz[i] * z));
            if (shortfall > 1.0e-4f) feasible = false;
            violation += shortfall * shortfall;
        }
        if (feasible) {
            if (!found_feasible || length_sq < best_length_sq) {
                best_x = x;
                best_z = z;
                best_length_sq = length_sq;
                found_feasible = true;
            }
        } else if (!found_feasible &&
                   (violation < best_violation - 1.0e-5f ||
                    (std::fabs(violation - best_violation) <= 1.0e-5f &&
                     length_sq < best_length_sq))) {
            best_x = x;
            best_z = z;
            best_length_sq = length_sq;
            best_violation = violation;
        }
    };

    consider(0.0f, 0.0f);
    for (int i = 0; i < active_count; ++i) {
        consider(hx[i] * required[i], hz[i] * required[i]);
        for (int j = i + 1; j < active_count; ++j) {
            const float determinant = hx[i] * hz[j] - hz[i] * hx[j];
            if (std::fabs(determinant) <= 1.0e-4f) continue;
            const float x = (required[i] * hz[j] - hz[i] * required[j]) /
                            determinant;
            const float z = (hx[i] * required[j] - required[i] * hx[j]) /
                            determinant;
            consider(x, z);
        }
    }
    return {best_x, 0.0f, best_z};
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
    float minimum_gap,
    const Vec3* facing = nullptr,
    float model_scale = 1.0f
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
            normal, feet_gap, model_radius, model_height, 0.0f,
            facing, model_scale);
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
