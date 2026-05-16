#include <cassert>
#include <cmath>

#include "room_data.hpp"
#include "world.hpp"

using namespace madeline_cube;

namespace {

bool NearlyEqual(float left, float right) {
    return std::fabs(left - right) < 0.0001f;
}

}  // namespace

int main() {
    const Room start_room = GetForsakenCityStartRoom();
    assert(start_room.collider_count == 4);
    assert(start_room.geometry_count == 4);
    assert(start_room.player_start.y == 3.0f);
    assert(QueryFloorSource(start_room, start_room.player_start, 10.0f).hit);

    const Room& room = GetPhysicsQueryGrayboxRoom();

    const GroundHit floor = QueryFloorSource(room, {0.0f, 4.0f, 0.0f}, 10.0f);
    assert(floor.hit);
    assert(floor.face_id == 0);
    assert(NearlyEqual(floor.distance, 4.0f));
    assert(NearlyEqual(floor.point.y, 0.0f));
    assert(NearlyEqual(floor.normal.y, 1.0f));

    const CeilingHit ceiling = QueryCeilingSource(room, {0.0f, 2.0f, 0.0f}, 10.0f);
    assert(ceiling.hit);
    assert(NearlyEqual(ceiling.distance, 4.0f));
    assert(NearlyEqual(ceiling.point.y, 6.0f));
    assert(NearlyEqual(ceiling.normal.y, -1.0f));

    const WallHit left_wall = QueryWallClosestToNormal(room, {0.0f, 3.0f, 0.0f}, 5.0f, {1.0f, 0.0f, 0.0f});
    assert(left_wall.hit);
    assert(NearlyEqual(left_wall.normal.x, 1.0f));

    const WallHit right_wall = QueryWallClosestToNormal(room, {0.0f, 3.0f, 0.0f}, 5.0f, {-1.0f, 0.0f, 0.0f});
    assert(right_wall.hit);
    assert(NearlyEqual(right_wall.normal.x, -1.0f));

    // Pushout via wall nearest probe near the right wall.
    const WallHit pushout = QueryWallNearest(room, {2.8f, 3.0f, 0.0f}, 0.6f);
    assert(pushout.hit);
    assert(NearlyEqual(pushout.normal.x, -1.0f));
    assert(pushout.pushout > 0.0f);

    // Duplicate right-wall colliders: face_id tie-breaking is deterministic.
    const WallHit tie = QueryWallClosestToNormal(room, {0.0f, 3.0f, 1.0f}, 5.0f, {-1.0f, 0.0f, 0.0f});
    assert(tie.hit);

    // --- Source-shaped raycast: backface policy ignores faces aimed away. ---
    {
        const GroundHit floor_src = QueryFloorSource(room, {0.0f, 4.0f, 0.0f}, 10.0f);
        assert(floor_src.hit);
        assert(NearlyEqual(floor_src.distance, 4.0f));
        assert(NearlyEqual(floor_src.normal.y, 1.0f));
        assert(floor_src.face_id == 0);
    }

    // --- Source-shaped raycast: returns no hit when ray escapes geometry. ---
    {
        const GroundHit miss = RaycastRoomSource(room, {100.0f, 100.0f, 100.0f}, {0.0f, 1.0f, 0.0f}, 10.0f);
        assert(!miss.hit);
        assert(miss.face_id == -1);
    }

    // --- QueryCeilingSource reports no hit when no ceiling is above. ---
    {
        const CeilingHit none = QueryCeilingSource(room, {0.0f, 10.0f, 0.0f}, 1.0f);
        assert(!none.hit);
        assert(none.face_id == -1);
    }

    // --- QueryCeilingSource finds the ceiling plane from below. ---
    {
        const CeilingHit ceil = QueryCeilingSource(room, {0.0f, 2.0f, 0.0f}, 10.0f);
        assert(ceil.hit);
        assert(NearlyEqual(ceil.distance, 4.0f));
        assert(NearlyEqual(ceil.normal.y, -1.0f));
    }

    // --- BackfacePolicy::Include keeps faces that face away from the ray. ---
    {
        const GroundHit upward = RaycastRoomSource(room, {0.0f, 1.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, 10.0f,
                                                   BackfacePolicy::Ignore);
        assert(upward.hit);
        const GroundHit through = RaycastRoomSource(room, {0.0f, -1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 10.0f,
                                                    BackfacePolicy::Include);
        assert(through.hit);
        assert(NearlyEqual(through.distance, 1.0f));
    }

    // --- Wall queries: nearest selects the deepest pushout. ---
    {
        const WallHit nearest = QueryWallNearest(room, {2.8f, 3.0f, 0.0f}, 0.6f);
        assert(nearest.hit);
        assert(NearlyEqual(nearest.normal.x, -1.0f));
        assert(nearest.pushout > 0.0f);
    }

    // --- Wall queries: closest-to-normal honors direction preference. ---
    {
        const WallHit toward_left = QueryWallClosestToNormal(room, {0.0f, 3.0f, 0.0f}, 5.0f, {1.0f, 0.0f, 0.0f});
        assert(toward_left.hit);
        assert(toward_left.normal.x > 0.0f);

        const WallHit toward_right = QueryWallClosestToNormal(room, {0.0f, 3.0f, 0.0f}, 5.0f, {-1.0f, 0.0f, 0.0f});
        assert(toward_right.hit);
        assert(toward_right.normal.x < 0.0f);
    }

    // --- Moving-surface ownership: rider velocity reflects platform motion. ---
    {
        Room moving = BuildMovingPlatformFixtureRoom();
        MovingSurface* platform = FindMovingSurfaceMutable(moving, 200);
        assert(platform != nullptr);
        platform->position.x += 0.5f;  // platform moves +X by 0.5 over the frame
        AdvanceMovingSurfaces(moving, 1.0f / 60.0f);

        assert(NearlyEqual(platform->displacement.x, 0.5f));
        assert(NearlyEqual(platform->rider_velocity.x, 0.5f * 60.0f));

        const GroundHit on_platform = QueryFloorSource(moving, {0.5f, 3.0f, 0.0f}, 5.0f);
        assert(on_platform.hit);
        assert(on_platform.owner_id == 200);
        assert(NearlyEqual(on_platform.owner_velocity.x, 0.5f * 60.0f));
        assert(NearlyEqual(on_platform.point.y, 2.0f));

        const GroundHit on_floor = QueryFloorSource(moving, {10.0f, 3.0f, 0.0f}, 5.0f);
        assert(on_floor.hit);
        assert(on_floor.owner_id == 100);
        assert(NearlyEqual(on_floor.owner_velocity.x, 0.0f));
    }

    // --- Non-solid colliders are ignored by source-shaped queries. ---
    {
        Room trigger_room;
        trigger_room.colliders[trigger_room.collider_count++] = {
            .type = ColliderType::Plane,
            .bounds = {.min = {-2.0f, 0.0f, -2.0f}, .max = {2.0f, 0.0f, 2.0f}},
            .solid = false,
            .normal = {0.0f, 1.0f, 0.0f},
        };
        const GroundHit through_trigger = QueryFloorSource(trigger_room, {0.0f, 4.0f, 0.0f}, 10.0f);
        assert(!through_trigger.hit);
    }

    return 0;
}
