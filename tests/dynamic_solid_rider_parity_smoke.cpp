#include <cassert>
#include <cmath>
#include <cstdio>

#include "gameplay/physics/coll_mesh.hpp"
#include "gameplay/player/player_controller.hpp"
#include "gameplay/world/dynamic_solid_geometry.hpp"
#include "gameplay/world/dynamic_solid_world.hpp"
#include "gameplay/world/map_runtime.hpp"

using namespace madeline_cube;

namespace {

Vec3 Add(const Vec3& a, const Vec3& b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

Vec3 Sub(const Vec3& a, const Vec3& b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Vec3 ToWorld(const Vec3& local, const DynamicSolidView& view) {
    const float c = std::cos(view.yaw_radians);
    const float s = std::sin(view.yaw_radians);
    return {view.position.x + c * local.x + s * local.z,
            view.position.y + local.y,
            view.position.z - s * local.x + c * local.z};
}

struct UpFace {
    uint16_t actor_index = 0;
    uint32_t face_index = 0;
    Vec3 local_center;
};

bool FindUpFace(const DynamicSolidGeometry& geometry, UpFace& out) {
    const DynamicSolidFace* faces = geometry.Faces();
    const DynamicSolidVertex* vertices = geometry.Vertices();
    for (uint16_t ai = 0; ai < geometry.ActorCount(); ++ai) {
        const DynamicSolidSpec& actor = geometry.Actors()[ai];
        for (uint32_t fi = actor.dsol_first_face;
             fi < actor.dsol_first_face + actor.dsol_face_count; ++fi) {
            const DynamicSolidFace& face = faces[fi];
            if (!(face.collision_material & physics::MAT_SOLID) ||
                face.vertex_count < 3 || face.cms_source_face_id == 0) continue;
            const Vec3 raw = physics::Cross(
                physics::Sub(vertices[face.first_vertex + 1].position,
                             vertices[face.first_vertex].position),
                physics::Sub(vertices[face.first_vertex + 2].position,
                             vertices[face.first_vertex].position));
            if (physics::Len2(raw) < 1e-8f || physics::Norm(raw).y < 0.99f)
                continue;
            Vec3 center{};
            for (uint32_t vi = 0; vi < face.vertex_count; ++vi) {
                const Vec3& point = vertices[face.first_vertex + vi].position;
                center.x += point.x / face.vertex_count;
                center.y += point.y / face.vertex_count;
                center.z += point.z / face.vertex_count;
            }
            out = {ai, fi, center};
            return true;
        }
    }
    return false;
}

Vec3 MoveFaceCenter(DynamicSolidGeometry& geometry, const UpFace& face,
                    const Vec3& target_center) {
    const DynamicSolidSpec& actor = geometry.Actors()[face.actor_index];
    const DynamicSolidView before = geometry.Views()[face.actor_index];
    const Vec3 current_center = ToWorld(face.local_center, before);
    const Vec3 delta = Sub(target_center, current_center);
    const Vec3 target_position = Add(before.position, delta);
    assert(geometry.SetTransform(actor.source_id, target_position,
                                 before.yaw_radians));
    return geometry.Views()[face.actor_index].position;
}

bool Near(float a, float b, float eps = 0.001f) {
    return std::fabs(a - b) <= eps;
}

void AssertVec(const Vec3& actual, const Vec3& expected) {
    assert(Near(actual.x, expected.x));
    assert(Near(actual.y, expected.y));
    assert(Near(actual.z, expected.z));
}

}  // namespace

int main(int argc, char** argv) {
    assert(argc >= 3);
    MapRuntime runtime;
    assert(runtime.Init(argv[1], argv[2]));
    DynamicSolidGeometry& geometry = runtime.DynamicGeometry();
    const Room& room = runtime.Active()->room;
    UpFace face{};
    assert(FindUpFace(geometry, face));

    // Put one authored source face far beyond map bounds so carry checks have
    // no unrelated static or dynamic contacts.
    const Vec3 isolated_center{250000.0f, 1200.0f, 250000.0f};
    MoveFaceCenter(geometry, face, isolated_center);
    const DynamicSolidSpec& actor = geometry.Actors()[face.actor_index];
    const int owner_id = 0x40000000 | face.actor_index;
    const Vec3 platform_center = ToWorld(face.local_center,
                                         geometry.Views()[face.actor_index]);
    const GroundHit support = QueryFloorSource(
        room, Add(platform_center, {0.0f, 2.0f, 0.0f}), 5.0f);
    assert(support.hit && support.owner_id == owner_id);

    PlayerController controller;
    PlayerState player{};
    player.position = Add(platform_center, {0.0f, 11.25f, 0.0f});
    GroundHit player_support{};
    assert(controller.Mover().GroundCheck(player, room, nullptr, nullptr,
                                          &player_support));
    assert(player_support.owner_id == owner_id);

    const Vec3 displacement{3.0f, 2.0f, -1.0f};
    const Vec3 platform_velocity{40.0f, 60.0f, -20.0f};
    const Vec3 player_before = player.position;
    const Vec3 platform_before = geometry.Views()[face.actor_index].position;
    const uint32_t reinsert_before =
        geometry.Views()[face.actor_index].reinsert_ordinal;
    const DynamicSolidMoveResult carried = MoveDynamicSolidTo(
        geometry, actor.source_id, Add(platform_before, displacement),
        platform_velocity, controller, player, room);
    assert(carried.found && carried.moved && carried.carried_player);
    AssertVec(player.position, Add(player_before, displacement));
    assert(geometry.Views()[face.actor_index].collidable);
    assert(geometry.Views()[face.actor_index].reinsert_ordinal > reinsert_before);
    AssertVec(geometry.Views()[face.actor_index].velocity, platform_velocity);
    AssertVec(player.platform_carry.stored_velocity, platform_velocity);
    assert(Near(player.platform_carry.time_remaining, 0.10f));
    AssertVec(player.climb.corner_from, displacement);
    AssertVec(player.climb.corner_to, displacement);

    const Vec3 moved_center = ToWorld(face.local_center,
                                      geometry.Views()[face.actor_index]);
    const GroundHit moved_support = QueryFloorSource(
        room, Add(moved_center, {0.0f, 2.0f, 0.0f}), 5.0f);
    assert(moved_support.hit && moved_support.owner_id == owner_id);
    AssertVec(moved_support.owner_velocity, platform_velocity);

    // No-op MoveTo updates source velocity but neither carries nor reinserts.
    const uint32_t stable_ordinal =
        geometry.Views()[face.actor_index].reinsert_ordinal;
    const Vec3 player_after_carry = player.position;
    const Vec3 idle_velocity{1.0f, 2.0f, 3.0f};
    const DynamicSolidMoveResult no_op = MoveDynamicSolidTo(
        geometry, actor.source_id, geometry.Views()[face.actor_index].position,
        idle_velocity, controller, player, room);
    assert(no_op.found && !no_op.moved && !no_op.carried_player);
    assert(geometry.Views()[face.actor_index].reinsert_ordinal == stable_ordinal);
    AssertVec(player.position, player_after_carry);
    AssertVec(geometry.Views()[face.actor_index].velocity, idle_velocity);

    // A released, unsupported player is not carried by proximity alone.
    player.position = Add(moved_center, {0.0f, 30.0f, 0.0f});
    const Vec3 released_position = player.position;
    const Vec3 next_platform_position =
        Add(geometry.Views()[face.actor_index].position, {0.0f, 1.0f, 0.0f});
    const DynamicSolidMoveResult released = MoveDynamicSolidTo(
        geometry, actor.source_id, next_platform_position, platform_velocity,
        controller, player, room);
    assert(released.found && released.moved && !released.carried_player);
    AssertVec(player.position, released_position);

    // Climb attachment is owner identity, not a foot-support test. Owner IDs
    // live above 16 bits, so preserve the complete encoded identity.
    player.position = {400000.0f, 400000.0f, 400000.0f};
    player.movement_state = PlayerMovementState::Climbing;
    player.climb.wall_surface_id = owner_id;
    const Vec3 climbing_before = player.position;
    const Vec3 climb_delta{-2.0f, 1.0f, 4.0f};
    const DynamicSolidMoveResult climbing = MoveDynamicSolidTo(
        geometry, actor.source_id,
        Add(geometry.Views()[face.actor_index].position, climb_delta),
        platform_velocity, controller, player, room);
    assert(climbing.found && climbing.moved && climbing.carried_player);
    AssertVec(player.position, Add(climbing_before, climb_delta));

    // Real-map hitstop integration: scene gates the world tick before either
    // Player or dynamic-solid movement. Two consumed ticks leave rider,
    // actor transform, collision reinsertion, and gameplay timers unchanged.
    const float dt = 1.0f / 60.0f;
    player.world_hitstop_remaining = 2.0f * dt;
    player.dash_time_remaining = 0.17f;
    player.platform_carry.time_remaining = 0.08f;
    const Vec3 frozen_player_position = player.position;
    const Vec3 frozen_solid_position = geometry.Views()[face.actor_index].position;
    const uint32_t frozen_reinsert_ordinal =
        geometry.Views()[face.actor_index].reinsert_ordinal;
    const Vec3 frozen_corner_from = player.climb.corner_from;
    const Vec3 frozen_corner_to = player.climb.corner_to;
    int consumed_hitstop_ticks = 0;
    for (int tick = 0; tick < 2; ++tick) {
        if (controller.ConsumeWorldHitstopTick(player, dt)) {
            ++consumed_hitstop_ticks;
            continue;
        }
        MoveDynamicSolidTo(
            geometry, actor.source_id,
            Add(geometry.Views()[face.actor_index].position, {1.0f, 0.0f, 0.0f}),
            platform_velocity, controller, player, room);
    }
    assert(consumed_hitstop_ticks == 2);
    AssertVec(player.position, frozen_player_position);
    AssertVec(geometry.Views()[face.actor_index].position, frozen_solid_position);
    assert(geometry.Views()[face.actor_index].reinsert_ordinal ==
           frozen_reinsert_ordinal);
    AssertVec(player.climb.corner_from, frozen_corner_from);
    AssertVec(player.climb.corner_to, frozen_corner_to);
    assert(player.dash_time_remaining == 0.17f);
    assert(player.platform_carry.time_remaining == 0.08f);
    assert(player.world_hitstop_remaining <= 0.0f);

    const DynamicSolidMoveResult resumed = MoveDynamicSolidTo(
        geometry, actor.source_id,
        Add(frozen_solid_position, {1.0f, 0.0f, 0.0f}),
        platform_velocity, controller, player, room);
    assert(resumed.found && resumed.moved && resumed.carried_player);
    AssertVec(player.position, Add(frozen_player_position, {1.0f, 0.0f, 0.0f}));

    std::puts("PASS: dynamic_solid_rider_parity_smoke");
}
