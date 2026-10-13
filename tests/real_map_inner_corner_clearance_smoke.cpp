// Traverse the finite pillar corner near Start. Authored outward winding
// makes face5186 (-X) a reachable front-side outer wall, so the source path
// must ease onto it with bounded continuous movement and no body penetration.
#include <cmath>
#include <cstdio>

#include "gameplay/physics/coll_mesh.hpp"
#include "gameplay/player/player_controller.hpp"
#include "gameplay/world/mappack_loader.hpp"
#include "gameplay/world/world.hpp"

using namespace madeline_cube;
using namespace madeline_cube::physics;

namespace {

constexpr float kDt = 1.0f / 60.0f;
constexpr Vec3 kTargetNormal{-1.0f, 0.0f, 0.0f};

}  // namespace

int main(int argc, char** argv) {
    const char* pack_path = argc > 1 ? argv[1]
        : "build/bake-fc-1200/staging/forsyken-city.mappack";
    const char* mesh_path = argc > 2 ? argv[2]
        : "build/bake-fc-1200/staging/forsyken-city.colmesh";
    MapSpecV2 map{};
    if (!LoadMapPackV2(pack_path, map)) {
        std::printf("FAIL: cannot load %s\n", pack_path);
        return 1;
    }
    const V2SpawnSpec* start = map.FindStartSpawn();
    CollMesh* mesh = LoadCollMesh(mesh_path);
    if (!start || !mesh) {
        std::printf("FAIL: missing Start or collision mesh\n");
        FreeCollMesh(mesh);
        return 1;
    }

    Room room{};
    room.coll_mesh = mesh;
    MovementConfig config{};
    PlayerController controller(config);
    PlayerState state{};
    state.position = {
        -14.2f,
        start->position.y + config.player_half_height + 0.2f,
        55.2f,
    };
    state.prev_position = state.position;
    state.facing = state.target_facing = state.last_facing = {0.0f, 0.0f, -1.0f};

    PlayerInput input{};
    input.climb_held = true;
    auto tick = [&](const PlayerInput& in) {
        controller.BeginMoveTick(state);
        auto context = controller.TimerInputPhase(state, in, {0.0f, 0.0f, 1.0f}, kDt);
        controller.StatePhase(state, in, context, kDt, &room);
        controller.MovePhase(state, kDt, &room, &context);
        return context.corner_triggered;
    };
    tick(input);
    if (state.movement_state != PlayerMovementState::Climbing ||
        state.climb.wall_normal.z < 0.8f) {
        std::printf("FAIL: could not enter real face 5188; state=%d face=%d n=(%.2f,%.2f,%.2f)\n",
                    static_cast<int>(state.movement_state), state.climb.grip_face_id,
                    state.climb.wall_normal.x, state.climb.wall_normal.y,
                    state.climb.wall_normal.z);
        FreeCollMesh(mesh);
        return 1;
    }

    // Check the actual baked contact without running a duplicate query in the
    // game path: isolated fixture calls must have bounded overlap-only cost.
    CollisionQueryCounters query_cost{};
    room.query_counters = &query_cost;
    const Vec3 waist = ProbeAboveFeet(state.position, config.player_half_height,
                                     config.climb_waist_offset);
    const WallHit grip = QueryClimbWall(room, waist, config.climb_check_dist,
                                       state.climb.wall_normal);
    room.query_counters = nullptr;
    if (!grip.hit || grip.plane_normal.z < 0.8f ||
        query_cost.overlap_queries != 1 || query_cost.raycasts != 0) {
        std::printf("FAIL: baked grip query contract/cost face=%d overlaps=%u rays=%u\n",
                    grip.face_id, query_cost.overlap_queries, query_cost.raycasts);
        FreeCollMesh(mesh);
        return 1;
    }

    // A separate actual backside candidate must remain unreachable. This
    // point lies inside the pillar just behind its left face; only Include
    // may see that authored -X face while gameplay culling must reject it.
    const Vec3 behind_left_face = {-18.2f,waist.y,45.0f};
    const GroundHit backface = RaycastRoomSource(room,behind_left_face,{-1,0,0},2,
                                                BackfacePolicy::Include);
    WallHit unreachable{};
    if (!backface.hit || backface.normal.x > -.99f ||
        QueryWallRay(room,behind_left_face,{-1,0,0},2,&unreachable)) {
        std::printf("FAIL: authored backside wall incorrectly reachable\n");
        FreeCollMesh(mesh);
        return 1;
    }

    input.move = {1.0f, 0.0f};  // initial source sign1: raw +X walks world -X
    bool released = false;
    int corner_events = 0;
    float min_authored_side = INFINITY;
    bool wrong_side_corner_adopted = false;
    float max_step = 0.0f;
    float max_body_pushout = 0.0f;
    for (int i = 0; i < 30; ++i) {
        const Vec3 tick_start = state.position;
        const bool corner_triggered = tick(input);
        const float dx = state.position.x - tick_start.x;
        const float dy = state.position.y - tick_start.y;
        const float dz = state.position.z - tick_start.z;
        const float tick_distance = std::sqrt(dx * dx + dy * dy + dz * dz);
        max_step = std::max(max_step, tick_distance);
        if (corner_triggered) {
            ++corner_events;
            const int fid = state.climb.grip_face_id;
            const CollTriangle& triangle = mesh->triangles[fid];
            const Vec3& a = mesh->world_verts[triangle.i0];
            const Vec3 raw = Norm(Cross(Sub(mesh->world_verts[triangle.i1],a),
                                       Sub(mesh->world_verts[triangle.i2],a)));
            const Vec3 waist_at_adopt = ProbeAboveFeet(state.position,
                config.player_half_height,config.climb_waist_offset);
            const float authored_side = Dot(raw,Sub(waist_at_adopt,a));
            min_authored_side = std::min(min_authored_side,authored_side);
            wrong_side_corner_adopted |= authored_side < -0.001f;
        }
        if (state.movement_state == PlayerMovementState::Climbing) {
            const Vec3 feet = {state.position.x,
                               state.position.y - config.player_half_height,
                               state.position.z};
            const Vec3 probes[] = {
                {feet.x, feet.y + config.climb_waist_offset, feet.z},
                {feet.x, feet.y + config.climb_head_offset, feet.z},
            };
            for (const Vec3& probe : probes) {
                const WallHit overlap = QueryWallPushout(
                    room, probe, config.climb_pushout_dist);
                if (overlap.hit) {
                    max_body_pushout = std::max(max_body_pushout,
                                                 overlap.pushout);
                }
            }
        } else {
            released = true;
            break;
        }
    }
    const bool arrived = Dot(state.climb.wall_normal,kTargetNormal) > .99f &&
                         state.climb.corner_ease <= .001f;
    std::printf("[real-map-inner-corner] released=%u wrong_side_adopt=%u events=%d "
                "arrived=%u authored_side=%.3f max_step=%.3f max_body_pushout=%.3f "
                "end=(%.2f,%.2f,%.2f)\n",
                released ? 1u : 0u, wrong_side_corner_adopted ? 1u : 0u,
                corner_events,arrived ? 1u : 0u,min_authored_side,max_step,max_body_pushout,
                state.position.x,state.position.y,state.position.z);
    FreeCollMesh(mesh);
    // Source corner easing plus final repair spans both horizontal axes;
    // retain a diagonal displacement bound instead of a vertical-climb rate.
    return !released && corner_events == 1 && arrived && !wrong_side_corner_adopted &&
           max_step <= std::sqrt(2.0f) * config.climb_speed * kDt + 0.05f &&
           max_body_pushout <= 0.10f ? 0 : 1;
}
