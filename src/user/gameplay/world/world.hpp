#pragma once

#include <cstdint>

#include "gameplay/math_types.hpp"
#include "gameplay/player/player_state.hpp"

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
    Vec3 normal = {0.0f, 1.0f, 0.0f};
    Vec3 velocity = {0.0f, 0.0f, 0.0f};
    // Explicit point on the plane for sloped Plane colliders.  When the plane
    // is axis-aligned (normal == +Y, all bounds corners coplanar) callers may
    // leave this zero and RaycastPlane falls back to a bounds corner.  Slopes
    // must set this so the plane equation references an actual on-plane point.
    Vec3 plane_origin = {0.0f, 0.0f, 0.0f};
    bool has_plane_origin = false;
    int face_id = -1;   // stable ID for deterministic tie breaking
    int owner_id = -1;  // owning surface / actor
};

// Moving surface owner. Holds per-frame displacement and rider velocity
// storage metadata for platforms whose colliders move under the player.
// Colliders point at a surface via their owner_id; surfaces here are queried
// by id from Room::moving_surfaces.
struct MovingSurface {
    int id = -1;
    Vec3 position;             // current world position of the surface origin
    Vec3 last_position;        // position at the previous tick
    Vec3 displacement;         // position - last_position, written by World::AdvanceSurfaces
    Vec3 rider_velocity;       // velocity exposed to riders for stored-platform-velocity
    float carry_storage_time = 0.0f;  // expiry window for rider stored velocity
};

// Source-shaped query outputs consumed by player, camera, climb, and platform logic.
struct GroundHit {
    bool hit = false;
    Vec3 point;
    Vec3 normal = {0.0f, 1.0f, 0.0f};
    float distance = 0.0f;
    int face_id = -1;
    int owner_id = -1;
    Vec3 owner_velocity;
};

struct WallHit {
    bool hit = false;
    Vec3 point;
    Vec3 normal;
    float pushout = 0.0f;
    int face_id = -1;
    int owner_id = -1;
    Vec3 owner_velocity;
};

struct CeilingHit {
    bool hit = false;
    Vec3 point;
    Vec3 normal = {0.0f, -1.0f, 0.0f};
    float distance = 0.0f;
    int face_id = -1;
    int owner_id = -1;
    Vec3 owner_velocity;
};

// Backface policy for raycasts: ignore faces whose normal points away from the ray.
enum class BackfacePolicy : uint8_t {
    Ignore = 0,
    Include,
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
    static constexpr int kMaxColliders = 512;
    static constexpr int kMaxSpawns = 16;
    static constexpr int kMaxMovingSurfaces = 8;

    StaticGeometry geometry[kMaxGeometry];
    int geometry_count = 0;

    Collider colliders[kMaxColliders];
    int collider_count = 0;

    ActorSpawn spawns[kMaxSpawns];
    int spawn_count = 0;

    MovingSurface moving_surfaces[kMaxMovingSurfaces];
    int moving_surface_count = 0;

    Vec3 player_start = {0.0f, 3.0f, 0.0f};
    Vec3 checkpoint = {0.0f, 3.0f, 0.0f};
    float kill_plane_y = -20.0f;
};

// Source-shaped surface lookup. Returns null when no surface exists for owner_id.
const MovingSurface* FindMovingSurface(const Room& room, int owner_id);
MovingSurface* FindMovingSurfaceMutable(Room& room, int owner_id);

// Advance all moving surfaces by displacement_delta and move their owned
// colliders by the same delta. last_position is captured so displacement
// records the per-tick motion source code reads through Solid.Velocity.
void AdvanceMovingSurfaces(Room& room, float delta_seconds);

// Source-shaped raycast with backface policy and face/owner identity.
GroundHit RaycastRoomSource(const Room& room, const Vec3& origin, const Vec3& direction, float max_distance, BackfacePolicy backface = BackfacePolicy::Ignore);

// Source-shaped vertical probes.
GroundHit QueryFloorSource(const Room& room, const Vec3& origin, float max_distance);
CeilingHit QueryCeilingSource(const Room& room, const Vec3& origin, float max_distance);

// Source-shaped wall queries.
// Returns all wall hits within radius (up to a small fixed capacity).
static constexpr int kMaxWallHits = 8;
int QueryWalls(const Room& room, const Vec3& point, float radius, WallHit* out_hits, int max_hits);

// Nearest wall: the one with the largest pushout (most overlap).
WallHit QueryWallNearest(const Room& room, const Vec3& point, float radius);

// Wall whose normal is closest to the given direction.
WallHit QueryWallClosestToNormal(const Room& room, const Vec3& point, float radius, const Vec3& normal);


}  // namespace madeline_cube
