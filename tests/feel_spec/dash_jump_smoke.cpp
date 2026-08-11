// Host test for dash-jump fidelity (§31): dash on ground + jump -> dash-jump,
// momentum preserved, variable height.
//
// Build:
//   g++ -std=c++17 -Isrc/user tests/feel_spec/dash_jump_smoke.cpp \
//       src/user/gameplay/player/player_controller.cpp \
//       src/user/gameplay/runtime/math.cpp -o /tmp/dash_jump_smoke && /tmp/dash_jump_smoke
#include <cassert>
#include <cmath>

#include "../../src/user/gameplay/player/player_controller.hpp"

using namespace madeline_cube;

namespace {
constexpr float kDt = 1.0f / 60.0f;
float LengthXZ(const Vec3& v) { return std::sqrt(v.x * v.x + v.z * v.z); }
}

int main() {
    MovementConfig config;
    PlayerController controller(config);
    const Vec3 camera_forward = {0.0f, 0.0f, 1.0f};

    // Ground dash + jump -> dash-jump: ends dash, applies vertical velocity,
    // preserves horizontal momentum.
    PlayerState p;
    p.grounded = true;
    p.velocity = {0.0f, 0.0f, 0.0f};
    p.target_facing = {1.0f, 0.0f, 0.0f};
    p.last_facing = {1.0f, 0.0f, 0.0f};
    PlayerInput dash;
    dash.move = {1.0f, 0.0f};
    dash.dash_pressed = true;
    controller.Step(p, dash, camera_forward, kDt);
    assert(p.movement_state == PlayerMovementState::Dashing);
    assert(p.dashed_on_ground);

    // Wait for the no_dash_jump window (0.10s) to elapse while dashing.
    PlayerInput hold;
    hold.move = {1.0f, 0.0f};
    for (int i = 0; i < 7; ++i) controller.Step(p, hold, camera_forward, kDt);
    assert(p.movement_state == PlayerMovementState::Dashing);

    // Press jump during dash -> dash-jump.
    PlayerInput jump;
    jump.move = {1.0f, 0.0f};
    jump.jump_pressed = true;
    jump.jump_held = true;
    controller.Step(p, jump, camera_forward, kDt);
    assert(p.movement_state == PlayerMovementState::Normal);
    assert(p.velocity.y > 0.0f);  // vertical velocity applied
    assert(p.velocity.x > 0.0f);  // horizontal momentum preserved

    // Air dash must NOT dash-jump: dashed_on_ground is false.
    PlayerState air;
    air.grounded = false;
    air.velocity = {0.0f, 0.0f, 0.0f};
    air.target_facing = {1.0f, 0.0f, 0.0f};
    air.last_facing = {1.0f, 0.0f, 0.0f};
    PlayerInput air_dash;
    air_dash.move = {1.0f, 0.0f};
    air_dash.dash_pressed = true;
    controller.Step(air, air_dash, camera_forward, kDt);
    assert(air.movement_state == PlayerMovementState::Dashing);
    assert(!air.dashed_on_ground);
    PlayerInput air_jump;
    air_jump.move = {1.0f, 0.0f};
    air_jump.jump_pressed = true;
    air_jump.jump_held = true;
    controller.Step(air, air_jump, camera_forward, kDt);
    // Air dash + jump should NOT produce a dash-jump (no vertical from dash-jump).
    assert(air.velocity.y <= 0.0f || air.movement_state == PlayerMovementState::Dashing);

    return 0;
}
