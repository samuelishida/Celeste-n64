#include "collectible.hpp"

namespace madeline_cube {

bool TryCollect(CollectibleState& collectible, const Vec3& player_position) {
    if (collectible.collected) {
        return false;
    }

    const float dx = collectible.position.x - player_position.x;
    const float dy = collectible.position.y - player_position.y;
    const float dz = collectible.position.z - player_position.z;
    const float distance_squared = (dx * dx) + (dy * dy) + (dz * dz);
    const float radius_squared = collectible.pickup_radius * collectible.pickup_radius;

    if (distance_squared > radius_squared) {
        return false;
    }

    collectible.collected = true;
    return true;
}

}  // namespace madeline_cube

