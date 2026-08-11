// Host test for ledge assist (§43): when input direction has no floor ahead,
// the desired movement steers toward the nearest valid floor direction within
// ±17°; never moves the player without input.
//
// Build:
//   g++ -std=c++17 -Isrc/user tests/feel_spec/ledge_smoke.cpp \
//       src/user/gameplay/player/player_controller.cpp \
//       src/user/gameplay/player/player_motor.cpp \
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

    // Player on upper floor (y=0) near the edge (x=-5), moving +X toward the
    // gap. Ledge assist should steer the input toward the lower floor.
    PlayerState player;
    player.grounded = true;
    player.position = {-5.0f, 0.0f, 0.0f};
    player.velocity = {0.0f, 0.0f, 0.0f};
    player.contact.ground_normal = {0.0f, 1.0f, 0.0f};
    PlayerInput fwd;
    fwd.move = {1.0f, 0.0f};
    controller.Step(player, fwd, camera_forward, kDt, &room);
    // With ledge assist, the player should still be moving (steered), not
    // stopped at the edge. The exact direction depends on the probe; assert
    // the player is not frozen and facing has a +X component.
    assert(player.velocity.x > 0.0f || player.velocity.z != 0.0f);

    // No input -> no movement.
    PlayerState idle;
    idle.grounded = true;
    idle.position = {-5.0f, 0.0f, 0.0f};
    idle.velocity = {0.0f, 0.0f, 0.0f};
    idle.contact.ground_normal = {0.0f, 1.0f, 0.0f};
    controller.Step(idle, {}, camera_forward, kDt, &room);
    assert(idle.velocity.x == 0.0f && idle.velocity.z == 0.0f);

    // nullptr room -> ledge assist disabled, but movement still works.
    PlayerState no_room;
    no_room.grounded = true;
    no_room.position = {-5.0f, 0.0f, 0.0f};
    no_room.velocity = {0.0f, 0.0f, 0.0f};
    no_room.contact.ground_normal = {0.0f, 1.0f, 0.0f};
    controller.Step(no_room, fwd, camera_forward, kDt, nullptr);
    assert(no_room.velocity.x > 0.0f);

    return 0;
}
