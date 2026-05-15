#pragma once

#include <cstdint>

#include "math_types.hpp"
#include "player_state.hpp"

namespace madeline_cube {

// Axis-aligned bounding box for collision and culling.
struct AABB {
    Vec3 min;
    Vec3 max;

    bool Contains(const Vec3& point) const;
    bool Intersects(const AABB& other) const;
    bool IntersectsXZ(const Vec3& point) const;  // ignores Y
};

// Collision primitive types for graybox geometry.
enum class ColliderType : uint8_t {
    Box = 0,
    Plane,
};

struct Collider {
    ColliderType type = ColliderType::Box;
    AABB bounds;
    bool solid = true;  // true = blocks movement, false = trigger
};

// Static world geometry entry.
struct StaticGeometry {
    Vec3 position;
    Vec3 scale;
    uint32_t color = 0xFFFFFFFF;
};

// Actor spawn entry.
struct ActorSpawn {
    Vec3 position;
    uint16_t placeholder_id = 0;
};

// A graybox room composed of static geometry, collision, and spawns.
struct Room {
    static constexpr int kMaxGeometry = 64;
    static constexpr int kMaxColliders = 32;
    static constexpr int kMaxSpawns = 16;

    StaticGeometry geometry[kMaxGeometry];
    int geometry_count = 0;

    Collider colliders[kMaxColliders];
    int collider_count = 0;

    ActorSpawn spawns[kMaxSpawns];
    int spawn_count = 0;

    Vec3 player_start = {0.0f, 3.0f, 0.0f};
    Vec3 checkpoint = {0.0f, 3.0f, 0.0f};
    float kill_plane_y = -20.0f;
};

// Resolve player collision against all room colliders.
// Sets player.grounded and wall_left/wall_right.
void ResolveRoomCollision(PlayerState& player, const Room& room);

}  // namespace madeline_cube
