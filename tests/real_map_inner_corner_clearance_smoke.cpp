// Reproduce the finite wall edge near Forsaken City Start. A corner ray aimed
// from behind face 5186 can orient its normal backward and snap Madeline
// through the wall. This route must keep bounded steps and release cleanly
// when adjacent front-side geometry is unreachable.
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
constexpr Vec3 kWrongSideNormal{-1.0f, 0.0f, 0.0f};

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
        auto context = controller.TimerInputPhase(state, in, {0.0f, 0.0f, 1.0f}, kDt);
        controller.StatePhase(state, in, context, kDt, &room);
        controller.MovePhase(state, kDt, &room);
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

    input.move = {-1.0f, 0.0f};  // camera +Z maps left stick to world -X
    bool unsupported_edge_released = false;
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
        wrong_side_corner_adopted = wrong_side_corner_adopted ||
            (corner_triggered && Dot(kWrongSideNormal, state.climb.wall_normal) > 0.8f);
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
            unsupported_edge_released = true;
            break;
        }
    }
    std::printf("[real-map-inner-corner] released=%u wrong_side_adopt=%u max_step=%.3f "
                "max_body_pushout=%.3f end=(%.2f,%.2f,%.2f)\n",
                unsupported_edge_released ? 1u : 0u,
                wrong_side_corner_adopted ? 1u : 0u,
                max_step, max_body_pushout,
                state.position.x, state.position.y, state.position.z);
    FreeCollMesh(mesh);
    return unsupported_edge_released && !wrong_side_corner_adopted &&
           max_step <= 1.0f && max_body_pushout <= 0.10f ? 0 : 1;
}
