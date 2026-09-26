#include <algorithm>
#include <cassert>
#include <cmath>

#include "../../src/user/gameplay/input/input_system.hpp"
#include "../../src/user/gameplay/player/player_controller.hpp"
#include "../../src/user/gameplay/runtime/math.hpp"

using namespace madeline_cube;

int main() {
    PlayerController controller;
    constexpr float dt = 1.0f / 60.0f;

    // End-to-end raw N64 stick noise must be removed before the controller's
    // normalized direction + 30% minimum analog-speed target sees it.
    InputSystem move_input_system;
    const Vec2 tiny_stick = SampleStickAxis(5, 0, 80);
    const VirtualStickState filtered_tiny_stick = ApplyCircularDeadzone(
        tiny_stick, move_input_system.move_deadzone);
    PlayerInput tiny_move;
    tiny_move.move = filtered_tiny_stick.value;
    PlayerState tiny_stick_state;
    tiny_stick_state.grounded = true;
    tiny_stick_state.contact.ground_normal = {0.0f, 1.0f, 0.0f};
    controller.Step(tiny_stick_state, tiny_move,
                    {0.0f, 0.0f, 1.0f}, dt);
    assert(LengthXZ(tiny_stick_state.velocity) < 0.001f);

    PlayerState stop;
    stop.grounded = true;
    stop.velocity = {64.0f, 0.0f, 0.0f};
    for (int i = 0; i < 6; ++i) controller.Step(stop, {}, {0.0f, 0.0f, 1.0f}, dt);
    assert(LengthXZ(stop.velocity) < 0.1f);

    PlayerState turn;
    turn.grounded = true;
    turn.velocity = {64.0f, 0.0f, 0.0f};
    turn.target_facing = {1.0f, 0.0f, 0.0f};
    PlayerInput reverse;
    reverse.move = {-1.0f, 0.0f};
    for (int i = 0; i < 3; ++i) controller.Step(turn, reverse, {0.0f, 0.0f, 1.0f}, dt);
    assert(turn.target_facing.x < -0.9f);
    assert(turn.movement_state == PlayerMovementState::Skidding);

    // OG preserves grounded speed above trueMax when input stays aligned:
    // 100 -> 99 in one tick at PastMaxDeccel=60, not Acceleration=500.
    PlayerState ground_past_max;
    ground_past_max.grounded = true;
    ground_past_max.contact.ground_normal = {0.0f, 1.0f, 0.0f};
    ground_past_max.velocity = {100.0f, 0.0f, 0.0f};
    ground_past_max.target_facing = {1.0f, 0.0f, 0.0f};
    PlayerInput aligned_ground;
    aligned_ground.move = {1.0f, 0.0f};
    controller.Step(ground_past_max, aligned_ground,
                    {0.0f, 0.0f, 1.0f}, dt);
    assert(std::fabs(ground_past_max.velocity.x - 99.0f) < 0.02f);

    // OG rotates targetFacing at the slower above-max rate while carrying
    // speed above trueMax. One tick should turn by RotateSpeedAboveMax * dt.
    PlayerState ground_past_max_turn;
    ground_past_max_turn.grounded = true;
    ground_past_max_turn.contact.ground_normal = {0.0f, 1.0f, 0.0f};
    ground_past_max_turn.velocity = {100.0f, 0.0f, 0.0f};
    ground_past_max_turn.target_facing = {1.0f, 0.0f, 0.0f};
    PlayerInput diagonal_ground;
    diagonal_ground.move = {0.70710678f, 0.70710678f};
    auto turn_context = controller.TimerInputPhase(
        ground_past_max_turn, diagonal_ground, {0.0f, 0.0f, 1.0f}, dt);
    controller.StatePhase(ground_past_max_turn, diagonal_ground,
                          turn_context, dt);
    const float ground_turn_angle = std::acos(std::clamp(
        ground_past_max_turn.target_facing.x, -1.0f, 1.0f));
    assert(std::fabs(ground_turn_angle -
                     MovementConfig{}.rotate_speed_above_max * dt) < 0.01f);

    PlayerState air;
    air.grounded = false;
    air.velocity = {64.0f, 0.0f, 0.0f};
    const float before = LengthXZ(air.velocity);
    controller.Step(air, reverse, {0.0f, 0.0f, 1.0f}, dt);
    assert(LengthXZ(air.velocity) > 0.0f);
    assert(LengthXZ(air.velocity) < before);

    // OG applies 0.1x friction with no air input: 64 -> 24 after 30 ticks.
    PlayerState air_stop;
    air_stop.grounded = false;
    air_stop.position.y = 100.0f;
    air_stop.velocity = {64.0f, 0.0f, 0.0f};
    for (int i = 0; i < 30; ++i) {
        controller.Step(air_stop, {}, {0.0f, 0.0f, 1.0f}, dt);
    }
    assert(std::fabs(air_stop.velocity.x - 24.0f) < 0.1f);

    // OG `StNormalUpdate` applies AnalogMagnitude only to grounded max speed.
    // `RelativeMoveInput` is normalized, and airborne steering targets full
    // MaxSpeed even when stick magnitude is partial.
    MovementConfig config;
    PlayerController analog_controller(config);
    PlayerState partial_air;
    partial_air.grounded = false;
    partial_air.target_facing = {1.0f, 0.0f, 0.0f};
    PlayerInput partial_input;
    partial_input.move = {0.5f, 0.0f};
    for (int i = 0; i < 12; ++i) {
        analog_controller.Step(partial_air, partial_input,
                               {0.0f, 0.0f, 1.0f}, dt);
    }
    assert(LengthXZ(partial_air.velocity) >= config.run_speed - 0.1f);

    PlayerState partial_ground;
    partial_ground.grounded = true;
    partial_ground.contact.ground_normal = {0.0f, 1.0f, 0.0f};
    for (int i = 0; i < 12; ++i) {
        analog_controller.Step(partial_ground, partial_input,
                               {0.0f, 0.0f, 1.0f}, dt);
    }
    const float expected_ground_speed =
        config.run_speed * AnalogMagnitude(0.5f);
    assert(std::fabs(LengthXZ(partial_ground.velocity) - expected_ground_speed) < 0.1f);

    // OG air acceleration scales against targetFacing, not velocity direction.
    // Keep speed below RotateThreshold to isolate the acceleration mapping.
    PlayerState air_facing_scale;
    air_facing_scale.grounded = false;
    air_facing_scale.position.y = 100.0f;
    air_facing_scale.velocity = {0.0f, 0.0f, 8.0f};
    air_facing_scale.target_facing = {0.0f, 0.0f, -1.0f};
    PlayerInput forward;
    forward.move = {0.0f, 1.0f};
    analog_controller.Step(air_facing_scale, forward,
                           {0.0f, 0.0f, 1.0f}, dt);
    const float expected_facing_scaled_speed =
        8.0f + config.acceleration * config.air_accel_mult_min * dt;
    assert(std::fabs(air_facing_scale.velocity.z - expected_facing_scaled_speed) < 0.02f);
    assert(air_facing_scale.target_facing.z < -0.99f);

    // OG's past-max branch requires input aligned with velocity and reverses
    // the targetFacing accel map (aligned input uses AirAccelMultMin).
    PlayerState air_past_max;
    air_past_max.grounded = false;
    air_past_max.position.y = 100.0f;
    air_past_max.velocity = {0.0f, 0.0f, 100.0f};
    air_past_max.target_facing = {0.0f, 0.0f, 1.0f};
    analog_controller.Step(air_past_max, forward,
                           {0.0f, 0.0f, 1.0f}, dt);
    const float expected_past_max_speed =
        100.0f - config.past_max_deceleration * config.air_accel_mult_min * dt;
    assert(std::fabs(air_past_max.velocity.z - expected_past_max_speed) < 0.02f);

    // Above-max speed alone does not select PastMaxDeccel: opposing input
    // must use normal acceleration, as in OG's >= MaxSpeed && dot >= 0.7 gate.
    PlayerState air_past_max_reversal;
    air_past_max_reversal.grounded = false;
    air_past_max_reversal.position.y = 100.0f;
    air_past_max_reversal.velocity = {0.0f, 0.0f, 100.0f};
    air_past_max_reversal.target_facing = {0.0f, 0.0f, -1.0f};
    PlayerInput backward;
    backward.move = {0.0f, -1.0f};
    analog_controller.Step(air_past_max_reversal, backward,
                           {0.0f, 0.0f, 1.0f}, dt);
    const float expected_reversal_speed =
        100.0f - config.acceleration * dt;
    assert(std::fabs(air_past_max_reversal.velocity.z - expected_reversal_speed) < 0.02f);

    // OG air steering directly approaches input * MaxSpeed; it does not
    // rotate the target vector or overwrite targetFacing while airborne.
    PlayerState air_direct_steering;
    air_direct_steering.grounded = false;
    air_direct_steering.position.y = 100.0f;
    air_direct_steering.velocity = {20.0f, 0.0f, 0.0f};
    air_direct_steering.target_facing = {1.0f, 0.0f, 0.0f};
    analog_controller.Step(air_direct_steering, forward,
                           {0.0f, 0.0f, 1.0f}, dt);
    assert(air_direct_steering.velocity.x < 20.0f);
    assert(air_direct_steering.target_facing.x > 0.99f);

    // Match physics-autoplay's in-air sequence: steer camera-right for six
    // ticks, then release for four. This closes the integration gap between
    // the single-tick steering/friction assertions above and the ROM route.
    PlayerState air_route;
    air_route.grounded = false;
    air_route.position.y = 60.0f;
    air_route.velocity = {33.7f, -20.0f, -54.4f};
    air_route.target_facing = {0.0f, 0.0f, -1.0f};
    PlayerInput camera_right;
    camera_right.move = {1.0f, 0.0f};
    const float air_route_axis_start = air_route.velocity.x;
    for (int i = 0; i < 6; ++i) {
        analog_controller.Step(air_route, camera_right,
                              {0.0f, 0.0f, 1.0f}, dt);
    }
    const float air_route_axis_end = air_route.velocity.x;
    const float air_route_release_start = LengthXZ(air_route.velocity);
    assert(air_route_axis_end - air_route_axis_start >= 8.0f);
    for (int i = 0; i < 4; ++i) {
        analog_controller.Step(air_route, {}, {0.0f, 0.0f, 1.0f}, dt);
    }
    assert(!air_route.grounded);
    assert(air_route_release_start - LengthXZ(air_route.velocity) >= 4.0f);
    return 0;
}
