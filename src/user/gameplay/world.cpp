#include "world.hpp"

#include <cmath>

namespace madeline_cube {

bool AABB::Contains(const Vec3& point) const {
    return point.x >= min.x && point.x <= max.x &&
           point.y >= min.y && point.y <= max.y &&
           point.z >= min.z && point.z <= max.z;
}

bool AABB::Intersects(const AABB& other) const {
    return min.x <= other.max.x && max.x >= other.min.x &&
           min.y <= other.max.y && max.y >= other.min.y &&
           min.z <= other.max.z && max.z >= other.min.z;
}

bool AABB::IntersectsXZ(const Vec3& point) const {
    return point.x >= min.x && point.x <= max.x &&
           point.z >= min.z && point.z <= max.z;
}

void ResolveRoomCollision(PlayerState& player, const Room& room) {
    player.grounded = false;
    player.wall_left = false;
    player.wall_right = false;

    constexpr float kPlayerHalfHeight = 1.0f;
    constexpr float kPlayerRadius = 0.5f;

    for (int i = 0; i < room.collider_count; ++i) {
        const Collider& c = room.colliders[i];
        if (!c.solid) continue;

        if (c.type == ColliderType::Plane) {
            // Horizontal plane: ground
            const bool above_x = player.position.x >= c.bounds.min.x && player.position.x <= c.bounds.max.x;
            const bool above_z = player.position.z >= c.bounds.min.z && player.position.z <= c.bounds.max.z;
            const float floor_y = player.position.y - kPlayerHalfHeight;
            if (above_x && above_z && floor_y <= c.bounds.max.y && player.velocity.y <= 0.0f) {
                player.position.y = c.bounds.max.y + kPlayerHalfHeight;
                player.velocity.y = 0.0f;
                player.grounded = true;
            }
        } else if (c.type == ColliderType::Box) {
            // Wall collision: check if player is inside the box on XZ but at wall height
            const bool inside_xz = c.bounds.IntersectsXZ(player.position);
            const bool at_wall_height = (player.position.y >= c.bounds.min.y && player.position.y <= c.bounds.max.y);

            if (inside_xz && at_wall_height) {
                // Determine which side
                const float left_dist = std::fabs(player.position.x - c.bounds.min.x);
                const float right_dist = std::fabs(player.position.x - c.bounds.max.x);
                if (left_dist < right_dist && left_dist <= kPlayerRadius + 0.1f) {
                    player.wall_left = true;
                } else if (right_dist <= kPlayerRadius + 0.1f) {
                    player.wall_right = true;
                }
            }
        }
    }
}

}  // namespace madeline_cube
