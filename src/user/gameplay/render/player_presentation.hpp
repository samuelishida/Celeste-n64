#pragma once
#include "gameplay/math_types.hpp"
namespace madeline_cube {
// User-requested visual adaptation. The source event state remains exact;
// renderer and feet-pivot compensation consume the same reduced deformation.
constexpr float kPlayerSquashStrength = 0.25f;
constexpr Vec3 PlayerPresentationScale(const Vec3& source) {
    return {1.0f + (source.x-1.0f)*kPlayerSquashStrength,
            1.0f + (source.y-1.0f)*kPlayerSquashStrength,
            1.0f + (source.z-1.0f)*kPlayerSquashStrength};
}
}
