// Approach a supported wall from the real Forsaken City Start spawn, enter
// climb through normal queries, then wall-jump while climb is still held.
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
constexpr float kTau = 6.28318530717958647692f;
constexpr int kDirectionSamples = 16;
constexpr float kHeightOffsets[] = {0.0f, 4.0f, -4.0f, 8.0f, -8.0f};

float DotXZ(const Vec3& a, const Vec3& b) {
    return a.x * b.x + a.z * b.z;
}

struct TargetWall {
    WallHit hit{};
    Vec3 approach{};
    float start_gap = 0.0f;
};

bool FindTargetWall(const Room& room, const Vec3& waist,
                    float check_distance, TargetWall& target) {
    for (float distance = 2.0f; distance <= 120.0f; distance += 4.0f) {
        for (int band = 0; band < 5; ++band) {
            for (int sample = 0; sample < kDirectionSamples; ++sample) {
                const float angle = kTau * static_cast<float>(sample) /
                                    static_cast<float>(kDirectionSamples);
                const Vec3 direction = {std::cos(angle), 0.0f, std::sin(angle)};
                const Vec3 probe = {
                    waist.x + direction.x * distance,
                    waist.y + kHeightOffsets[band],
                    waist.z + direction.z * distance,
                };
                const WallHit hit = QueryClimbWall(
                    room, probe, check_distance,
                    {-direction.x, 0.0f, -direction.z});
                if (!hit.hit || DotXZ(hit.plane_normal, direction) >= -0.5f)
                    continue;

                float supported_height = 0.0f;
                for (float height = 3.0f; height <= 48.0f; height += 3.0f) {
                    const WallHit above = QueryClimbWall(
                        room, {hit.point.x, hit.point.y + height, hit.point.z},
                        check_distance, {-direction.x, 0.0f, -direction.z});
                    if (!above.hit || DotXZ(above.plane_normal, hit.plane_normal) < 0.8f)
                        break;
                    supported_height = height;
                }
                if (supported_height < 9.0f) continue;

                target.hit = hit;
                target.approach = {-hit.plane_normal.x, 0.0f, -hit.plane_normal.z};
                const float length = std::sqrt(DotXZ(target.approach, target.approach));
                if (length <= 1.0e-5f) return false;
                target.approach.x /= length;
                target.approach.z /= length;
                target.start_gap = Dot(hit.plane_normal,
                    {waist.x - hit.point.x, waist.y - hit.point.y,
                     waist.z - hit.point.z});
                return true;
            }
        }
    }
    return false;
}

float Distance3(const Vec3& a, const Vec3& b) {
    const float x = a.x - b.x;
    const float y = a.y - b.y;
    const float z = a.z - b.z;
    return std::sqrt(x * x + y * y + z * z);
}

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
        std::printf("FAIL: missing Start or cannot load %s\n", mesh_path);
        return 1;
    }

    Room room{};
    room.coll_mesh = mesh;
    MovementConfig config{};
    PlayerController controller(config);
    PlayerState state{};
    state.position = start->position;
    state.position.y += config.player_half_height + 0.2f;
    Vec3 spawn_pushout{};
    Vec3 spawn_normal{};
    if (controller.Mover().GroundCheck(state, room, &spawn_pushout,
                                       &spawn_normal)) {
        state.position.y += spawn_pushout.y + 0.2f;
    }
    state.prev_position = state.position;
    state.grounded = controller.Mover().GroundCheck(
        state, room, nullptr, &spawn_normal);
    state.contact.was_grounded = state.grounded;
    if (state.grounded) {
        state.contact.ground_normal = spawn_normal;
        state.contact.coyote_height = state.position.y;
        state.coyote_time_remaining = config.coyote_time;
    }

    const Vec3 waist = {
        state.position.x,
        state.position.y - config.player_half_height + config.climb_waist_offset,
        state.position.z,
    };
    TargetWall target{};
    if (!FindTargetWall(room, waist, config.climb_check_dist, target)) {
        std::printf("FAIL: no supported wall near manifest Start\n");
        FreeCollMesh(mesh);
        return 1;
    }
    state.facing = state.target_facing = state.last_facing = target.approach;

    int approach_ticks = 0;
    bool entered = false;
    float entry_gap = INFINITY;
    Vec3 before_jump{};
    for (; approach_ticks < 140 && !entered; ++approach_ticks) {
        const Vec3 current_waist = {
            state.position.x,
            state.position.y - config.player_half_height + config.climb_waist_offset,
            state.position.z,
        };
        const float gap = Dot(target.hit.plane_normal,
            {current_waist.x - target.hit.point.x,
             current_waist.y - target.hit.point.y,
             current_waist.z - target.hit.point.z});
        PlayerInput input{};
        input.move = {0.0f, 1.0f};
        if (state.wall_contact && gap <= config.climb_check_dist + 0.25f) {
            input.climb_held = true;
            entry_gap = gap;
        }
        controller.Step(state, input, target.approach, kDt, &room);
        entered = state.movement_state == PlayerMovementState::Climbing;
    }
    if (!entered) {
        std::printf("FAIL: climb entry not reached ticks=%d state=%d pos=(%.2f,%.2f,%.2f)\n",
                    approach_ticks, static_cast<int>(state.movement_state),
                    state.position.x, state.position.y, state.position.z);
        FreeCollMesh(mesh);
        return 1;
    }

    const Vec3 outward = state.climb.wall_normal;
    before_jump = state.position;
    PlayerInput jump{};
    jump.climb_held = true;
    jump.move = {0.0f, 1.0f};
    jump.jump_pressed = true;
    jump.jump_held = true;
    controller.Step(state, jump, target.approach, kDt, &room);

    const Vec3 expected = {
        outward.x * config.wall_jump_speed_x,
        config.wall_jump_speed_y,
        outward.z * config.wall_jump_speed_x,
    };
    const float velocity_error = Distance3(state.velocity, expected);
    const float displacement = Distance3(state.position, before_jump);
    const Vec3 current_waist = {
        state.position.x,
        state.position.y - config.player_half_height + config.climb_waist_offset,
        state.position.z,
    };
    const float exit_gap = Dot(outward,
        {current_waist.x - target.hit.point.x,
         current_waist.y - target.hit.point.y,
         current_waist.z - target.hit.point.z});
    const bool passed = state.movement_state == PlayerMovementState::Normal &&
        jump.jump_press_consumed && !state.auto_jump &&
        velocity_error <= 0.05f && displacement <= 3.0f &&
        exit_gap > config.climb_pushout_dist + 0.5f &&
        state.contact.ground_snap_cooldown_remaining > 0.0f;
    std::printf("[real-map-walljump] result=%s approach_ticks=%d entry_gap=%.2f "
                "face=%d launch=(%.2f,%.2f,%.2f) expected=(%.2f,%.2f,%.2f) "
                "error=%.3f displacement=%.3f exit_gap=%.2f consumed=%u\n",
                passed ? "PASS" : "FAIL", approach_ticks, entry_gap,
                target.hit.face_id, state.velocity.x, state.velocity.y,
                state.velocity.z, expected.x, expected.y, expected.z,
                velocity_error, displacement, exit_gap,
                jump.jump_press_consumed ? 1u : 0u);
    FreeCollMesh(mesh);
    return passed ? 0 : 1;
}
