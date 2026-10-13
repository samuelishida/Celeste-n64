#pragma once

#include <cmath>
#include <cstdint>

#include "gameplay/math_types.hpp"

namespace madeline_cube {

// Match std::lround's halfway-away-from-zero semantics without calling the
// comparatively expensive libm routine for every emitted hair coordinate.
inline bool PackPlayerHairCoordinate(float value, int16_t& packed) {
    if (!std::isfinite(value) || value < -32768.0f || value > 32767.0f) {
        return false;
    }
    const float biased = value + (value >= 0.0f ? 0.5f : -0.5f);
    packed = static_cast<int16_t>(static_cast<int32_t>(biased));
    return true;
}

struct PlayerHairRotation {
    float cosine = 1.0f;
    float sine = 0.0f;
};

struct PlayerHairNodeTransform {
    Vec3 node;
    float xz_scale = 1.0f;
    float y_scale = 1.0f;
    Vec3 squish_og;
    float cosine = 1.0f;
    float sine = 0.0f;
};

inline PlayerHairRotation MakePlayerHairRotation(const Vec3& forward_og) {
    const float angle = std::atan2(forward_og.y, forward_og.x);
    return {std::cos(angle), std::sin(angle)};
}

inline PlayerHairNodeTransform MakePlayerHairNodeTransform(
    const Vec3& node, int node_index, int node_count,
    const Vec3& squish_og, float roundness,
    const PlayerHairRotation& rotation) {
    const float p = static_cast<float>(node_index) / static_cast<float>(node_count);
    const float xz_start = 3.2f + (3.0f - 3.2f) * roundness;
    const float xz_scale = xz_start + (1.0f - xz_start) * p;
    const float y_start = 4.0f + (3.0f - 4.0f) * roundness;
    const float y_end = 2.0f + (1.0f - 2.0f) * roundness;
    const float y_scale = y_start + (y_end - y_start) * p;
    return {node, xz_scale, y_scale, squish_og,
            rotation.cosine, rotation.sine};
}

inline Vec3 PlayerHairSourceVertex(const Vec3& sphere_vertex,
                                   const PlayerHairNodeTransform& transform) {
    const float scaled_x = sphere_vertex.x * transform.xz_scale * transform.squish_og.x;
    const float scaled_y = sphere_vertex.y * transform.y_scale * transform.squish_og.y;
    const float scaled_z = sphere_vertex.z * transform.xz_scale * transform.squish_og.z;
    return {
        transform.node.x + scaled_x * transform.cosine - scaled_y * transform.sine,
        transform.node.y + scaled_x * transform.sine + scaled_y * transform.cosine,
        transform.node.z + scaled_z,
    };
}

// Exact source Hair.Prepare vertex transform in OG Z-up coordinates. Keep
// source roundness explicit: normal hair uses 0; FeatherStart/Feather use 1.
inline Vec3 PlayerHairSourceVertex(const Vec3& sphere_vertex,
                                   const Vec3& node,
                                   int node_index,
                                   int node_count,
                                   const Vec3& forward_og,
                                   const Vec3& squish_og,
                                   float roundness) {
    const PlayerHairRotation rotation = MakePlayerHairRotation(forward_og);
    const PlayerHairNodeTransform transform = MakePlayerHairNodeTransform(
        node, node_index, node_count, squish_og, roundness, rotation);
    return PlayerHairSourceVertex(sphere_vertex, transform);
}

}  // namespace madeline_cube
