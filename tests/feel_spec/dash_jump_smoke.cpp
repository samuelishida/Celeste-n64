// Host test for dash-jump fidelity (§31): dash on ground + jump -> dash-jump,
// momentum preserved, variable height.
//
// Built and run by tests/run_host_tests.sh with GAMEPLAY_LINK.
#include <cassert>
#include <cmath>

#include "../../src/user/gameplay/player/player_controller.hpp"

using namespace madeline_cube;

namespace {
constexpr float kDt = 1.0f / 60.0f;
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
    const float dash_speed_before_jump = p.velocity.x;
    auto jump_context = controller.TimerInputPhase(p, jump, camera_forward, kDt);
    controller.StatePhase(p, jump, jump_context, kDt);
    assert(p.movement_state == PlayerMovementState::Normal);
    assert(std::fabs(p.velocity.y - config.dash_jump_speed) < 0.001f);
    assert(std::fabs(p.velocity.x -
                     (dash_speed_before_jump + config.dash_jump_xy_boost)) < 0.001f);
    assert(std::fabs(p.dash_cooldown_remaining - config.dash_cooldown) < 0.001f);
    // OG StDashingUpdate returns after DashJump: dash animation/state survives
    // through this tick; normal grounded acceleration starts next tick.
    assert(p.locomotion_state == LocomotionState::Dash);

    // Late dash-jump is still a coyote takeoff in OG: restore the recorded
    // ground height, then consume stored moving-platform velocity just like
    // Jump()/SkidJump(). Current state is airborne but remains coyote-valid.
    PlayerState coyote_dash;
    coyote_dash.position = {0.0f, 12.0f, 0.0f};
    coyote_dash.grounded = false;
    coyote_dash.movement_state = PlayerMovementState::Dashing;
    coyote_dash.dashed_on_ground = true;
    coyote_dash.velocity = {0.0f, 0.0f, 0.0f};
    coyote_dash.target_facing = {1.0f, 0.0f, 0.0f};
    coyote_dash.last_facing = {1.0f, 0.0f, 0.0f};
    coyote_dash.contact.coyote_height = 10.0f;
    coyote_dash.coyote_time_remaining = 0.05f;
    coyote_dash.dash_time_remaining = 0.05f;
    coyote_dash.no_dash_jump_remaining = 0.0f;
    // Oversized carry also exercises OG's 300 horizontal / 180 upward clamps.
    coyote_dash.platform_carry.stored_velocity = {400.0f, 250.0f, 0.0f};
    coyote_dash.platform_carry.time_remaining = 0.05f;
    PlayerInput coyote_jump;
    coyote_jump.move = {1.0f, 0.0f};
    coyote_jump.jump_pressed = true;
    coyote_jump.jump_held = true;
    const auto coyote_context = controller.TimerInputPhase(
        coyote_dash, coyote_jump, camera_forward, kDt);
    auto coyote_step = coyote_context;
    controller.StatePhase(coyote_dash, coyote_jump, coyote_step, kDt);
    assert(std::fabs(coyote_dash.position.y - 10.0f) < 0.001f);
    assert(std::fabs(coyote_dash.velocity.x -
                     (config.dash_speed + config.dash_jump_xy_boost + 300.0f)) < 0.001f);
    assert(std::fabs(coyote_dash.velocity.y -
                     (config.dash_jump_speed + 180.0f)) < 0.001f);
    assert(coyote_dash.coyote_time_remaining == 0.0f);
    assert(coyote_dash.platform_carry.time_remaining == 0.0f);

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
    const float air_dash_speed = config.dash_speed / std::sqrt(1.16f);
    assert(std::fabs(air.velocity.x - air_dash_speed) < 0.02f);
    assert(std::fabs(air.velocity.y - air_dash_speed * 0.4f) < 0.02f);
    assert(std::fabs(std::sqrt(air.velocity.x * air.velocity.x +
                               air.velocity.y * air.velocity.y +
                               air.velocity.z * air.velocity.z) -
                     config.dash_speed) < 0.02f);
    PlayerInput air_jump;
    air_jump.move = {1.0f, 0.0f};
    air_jump.jump_pressed = true;
    air_jump.jump_held = true;
    controller.Step(air, air_jump, camera_forward, kDt);
    // Air dash has its own upward launch component, but cannot use grounded
    // DashJump's separate launch/boost path.
    assert(air.movement_state == PlayerMovementState::Dashing);
    assert(std::fabs(air.velocity.y - config.dash_jump_speed) > 1.0f);
    // Local hitstop covers later ticks, then active air-dash steering restores
    // OG's normalized upward vector. Expiry scales full 3D velocity by 0.75.
    controller.Step(air, {}, camera_forward, kDt);
    controller.Step(air, {}, camera_forward, kDt);
    assert(air.movement_state == PlayerMovementState::Dashing);
    assert(std::fabs(air.velocity.x - air_dash_speed) < 0.02f);
    assert(std::fabs(air.velocity.y - air_dash_speed * 0.4f) < 0.02f);

    air.dash_time_remaining = kDt * 0.5f;
    controller.Step(air, {}, camera_forward, kDt);
    assert(air.movement_state == PlayerMovementState::Normal);
    assert(std::fabs(air.velocity.x - air_dash_speed * 0.75f) < 0.03f);
    assert(std::fabs(air.velocity.y - air_dash_speed * 0.4f * 0.75f) < 0.03f);

    // Grounded dash remains horizontal and retains full DashSpeed.
    PlayerState ground_dash;
    ground_dash.grounded = true;
    PlayerInput ground_dash_input;
    ground_dash_input.move = {1.0f, 0.0f};
    ground_dash_input.dash_pressed = true;
    controller.Step(ground_dash, ground_dash_input, camera_forward, kDt);
    assert(std::fabs(ground_dash.velocity.x - config.dash_speed) < 0.02f);
    assert(std::fabs(ground_dash.velocity.y) < 0.02f);

    return 0;
}
