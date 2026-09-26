// Host test for OG ledge-avoidance: at a platform corner, turn toward nearby
// supported ground; preserve clear-path movement and never move without input.
//
// Build:
//   g++ -std=c++17 -Isrc/user tests/feel_spec/ledge_smoke.cpp \
//       src/user/gameplay/player/player_controller.cpp \
//       src/user/gameplay/player/player_movement.cpp \
//       src/user/gameplay/world/world.cpp \
//       src/user/gameplay/world/room_data.cpp \
//       src/user/gameplay/physics/coll_mesh.cpp \
//       src/user/gameplay/physics/geom.cpp \
//       src/user/gameplay/runtime/math.cpp -o /tmp/ledge_smoke && /tmp/ledge_smoke
#include <cassert>
#include <cmath>

#include "../../src/user/gameplay/player/player_controller.hpp"
#include "../../src/user/gameplay/world/room_data.hpp"

using namespace madeline_cube;

namespace {
constexpr float kDt = 1.0f / 60.0f;
}

int main() {
    Room room = BuildLedgeFixtureRoom();
    MovementConfig config;
    PlayerController controller(config);
    const Vec3 camera_forward = {0.0f, 0.0f, 1.0f};

    // Player center is half-height above the upper floor. At x=-3.9, the
    // forward probe misses while the +Z-rotated side probe still finds floor.
    PlayerState player;
    player.grounded = true;
    player.position = {-3.9f, config.player_half_height, 0.0f};
    player.velocity = {0.0f, 0.0f, 0.0f};
    player.contact.ground_normal = {0.0f, 1.0f, 0.0f};
    PlayerInput fwd;
    fwd.move = {1.0f, 0.0f};
    controller.Step(player, fwd, camera_forward, kDt, &room);
    assert(player.velocity.x > 0.0f);
    assert(player.velocity.z > 0.5f);  // OG rotates toward supported +Z side.

    // A clear path must not be turned by the heuristic.
    PlayerState clear_path;
    clear_path.grounded = true;
    clear_path.position = {-5.0f, config.player_half_height, 0.0f};
    clear_path.contact.ground_normal = {0.0f, 1.0f, 0.0f};
    controller.Step(clear_path, fwd, camera_forward, kDt, &room);
    assert(clear_path.velocity.x > 0.0f);
    assert(std::fabs(clear_path.velocity.z) < 0.0001f);

    // A solid wall ahead is not a ledge: keep the requested direction even
    // when a side-floor probe could otherwise steer around the platform edge.
    Room wall_ahead_room = room;
    wall_ahead_room.colliders[wall_ahead_room.collider_count++] = {
        .type = ColliderType::Box,
        .bounds = {.min = {0.0f, 0.0f, -50.0f}, .max = {0.5f, 20.0f, 50.0f}},
        .solid = true,
    };
    PlayerState wall_ahead;
    wall_ahead.grounded = true;
    wall_ahead.position = {-3.9f, config.player_half_height, 0.0f};
    wall_ahead.contact.ground_normal = {0.0f, 1.0f, 0.0f};
    controller.Step(wall_ahead, fwd, camera_forward, kDt, &wall_ahead_room);
    assert(wall_ahead.velocity.x > 0.0f);
    assert(std::fabs(wall_ahead.velocity.z) < 0.0001f);

    // No input -> no movement.
    PlayerState idle;
    idle.grounded = true;
    idle.position = {-3.9f, config.player_half_height, 0.0f};
    idle.velocity = {0.0f, 0.0f, 0.0f};
    idle.contact.ground_normal = {0.0f, 1.0f, 0.0f};
    controller.Step(idle, {}, camera_forward, kDt, &room);
    assert(idle.velocity.x == 0.0f && idle.velocity.z == 0.0f);

    // nullptr room -> edge steering disabled, but movement still works.
    PlayerState no_room;
    no_room.grounded = true;
    no_room.position = {-3.9f, config.player_half_height, 0.0f};
    no_room.velocity = {0.0f, 0.0f, 0.0f};
    no_room.contact.ground_normal = {0.0f, 1.0f, 0.0f};
    controller.Step(no_room, fwd, camera_forward, kDt, nullptr);
    assert(no_room.velocity.x > 0.0f);

    return 0;
}
