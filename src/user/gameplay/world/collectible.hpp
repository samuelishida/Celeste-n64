#pragma once

#include "gameplay/math_types.hpp"

namespace madeline_cube {

struct CollectibleState {
    Vec3 position;
    float pickup_radius = 10.0f;
    bool collected = false;
};

bool TryCollect(CollectibleState& collectible, const Vec3& player_position);

}  // namespace madeline_cube

