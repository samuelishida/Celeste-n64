#include <cassert>
#include <cmath>

#include "../src/user/gameplay/player/player_controller.hpp"

using namespace madeline_cube;

namespace {
constexpr float kDt = 1.0f / 60.0f;
bool Near(float a, float b, float eps = 0.02f) { return std::fabs(a - b) <= eps; }

Room MakeSupportRoom() {
    Room room;
    room.colliders[room.collider_count++] = {
        .type = ColliderType::Plane,
        .bounds = {.min = {-100.0f, 0.0f, -100.0f}, .max = {100.0f, 0.0f, 100.0f}},
        .solid = true,
        .normal = {0.0f, 1.0f, 0.0f},
        .face_id = 101,
    };
    return room;
}
}

int main() {
    MovementConfig config;
    PlayerController controller(config);
    const Vec3 camera_forward = {0.0f, 0.0f, 1.0f};

    PlayerState jump;
    jump.position.y = config.player_half_height;
    jump.grounded = true;
    jump.contact.coyote_height = jump.position.y;
    jump.coyote_time_remaining = config.coyote_time;
    PlayerInput held_jump;
    held_jump.jump_pressed = true;
    held_jump.jump_held = true;
    controller.Step(jump, held_jump, camera_forward, kDt);
    assert(Near(jump.velocity.y, config.jump_speed));
    // OG Player.cs:1163-1167 pins velocity to holdJumpSpeed while the hold
    // timer is live and Jump remains down; a held tick must not apply gravity.
    controller.Step(jump, {.jump_held = true}, camera_forward, kDt);
    assert(Near(jump.velocity.y, config.jump_speed));
    // Releasing Jump takes the gravity branch (Player.cs:1168-1180).
    controller.Step(jump, {}, camera_forward, kDt);
    assert(Near(jump.velocity.y, config.jump_speed - config.gravity * kDt));

    // OG StNormalUpdate checks tCoyote, not onGround, before consuming a jump
    // press. A stale grounded bit cannot bypass an expired coyote timer. The
    // buffered press remains live; LateContact refreshes support, so next tick
    // can jump and restores the just-recorded coyote height.
    {
        const Room floor = MakeSupportRoom();
        PlayerState expired;
        expired.position.y = config.player_half_height;
        expired.grounded = true;
        expired.contact.coyote_height = expired.position.y;
        PlayerInput buffered_press;
        buffered_press.jump_pressed = true;
        buffered_press.jump_held = true;
        controller.Step(expired, buffered_press, camera_forward, kDt, &floor);
        assert(!buffered_press.jump_press_consumed);
        assert(expired.velocity.y <= 0.0f);
        assert(expired.grounded && expired.coyote_time_remaining > 0.0f);

        expired.position.y += 2.0f;  // simulate source-visible support drift
        controller.Step(expired, buffered_press, camera_forward, kDt, &floor);
        assert(buffered_press.jump_press_consumed);
        assert(Near(expired.position.y, expired.contact.coyote_height + 1.5f));
        assert(expired.velocity.y > 0.0f);
    }

    // OG's `else if (WallJumpCheck())` still runs when tCoyote has expired,
    // even if the previous grounded bit remains true. A valid nearby wall
    // must consume the press and launch away instead of applying gravity.
    {
        Room floor_and_wall = MakeSupportRoom();
        floor_and_wall.colliders[floor_and_wall.collider_count++] = {
            .type = ColliderType::Box,
            .bounds = {.min = {3.0f, -100.0f, -100.0f},
                       .max = {6.0f, 100.0f, 100.0f}},
            .solid = true,
            .face_id = 202,
        };
        PlayerState wall_jump;
        wall_jump.position = {0.0f, config.player_half_height, 0.0f};
        wall_jump.grounded = true;
        wall_jump.target_facing = {1.0f, 0.0f, 0.0f};
        wall_jump.last_facing = wall_jump.target_facing;
        wall_jump.contact.coyote_height = wall_jump.position.y;
        PlayerInput press;
        press.jump_pressed = true;
        controller.Step(wall_jump, press, camera_forward, kDt, &floor_and_wall);
        assert(press.jump_press_consumed);
        assert(Near(wall_jump.velocity.x, -config.wall_jump_speed_x));
        assert(Near(wall_jump.velocity.y, config.wall_jump_speed_y));
    }

    // OG stateMachine.Update: entering Dash must not decrement active timers
    // or execute the Dashing update until the next fixed tick.
    PlayerState transition;
    transition.position = {0, config.player_half_height, 0};
    transition.grounded = true;
    Room support = MakeSupportRoom();
    PlayerInput press;
    press.dash_pressed = true;
    press.move = {1, 0};
    controller.BeginMoveTick(transition);
    auto step = controller.TimerInputPhase(transition, press, camera_forward, kDt);
    controller.StatePhase(transition, press, step, kDt, &support);
    assert(step.state_handler_count == 1);
    assert(transition.movement_state == PlayerMovementState::Dashing);
    assert(transition.world_hitstop_request > 0);
    assert(press.dash_press_consumed);
    controller.MovePhase(transition, kDt, &support, &step);

    PlayerState dash;
    dash.grounded = false;
    PlayerInput air_dash;
    air_dash.move = {1.0f, 0.0f};
    air_dash.dash_pressed = true;
    controller.Step(dash, air_dash, camera_forward, kDt);
    assert(dash.movement_state == PlayerMovementState::Dashing);
    // OG constants: DashSpeed=140 (Player.cs:30); SetDashSpeed uses
    // normalize((1,.4,0)) (Player.cs:1341-1348).
    const float air_dash_speed = 140.0f / std::sqrt(1.16f);
    assert(Near(dash.velocity.x, air_dash_speed));
    assert(Near(dash.velocity.y, air_dash_speed * 0.4f));

    // OG Player.cs:1020-1027 remaps grounded stick magnitude from [.4,.92]
    // to [.3,1] after saving trueMax. Use real support geometry so the
    // controller's contact phase cannot invalidate the grounded branch.
    {
        MovementConfig grounded_config;
        grounded_config.acceleration = 10000.0f;  // converge to this tick's target
        PlayerController grounded_controller(grounded_config);
        const Room floor = MakeSupportRoom();
        PlayerState partial_stick;
        partial_stick.position.y = grounded_config.player_half_height;
        partial_stick.grounded = true;
        PlayerInput half_stick;
        half_stick.move = {0.5f, 0.0f};
        grounded_controller.Step(partial_stick, half_stick, camera_forward, kDt, &floor);
        const float og_magnitude = 0.3f + ((0.5f - 0.4f) / (0.92f - 0.4f)) * 0.7f;
        // OG MaxSpeed is 64 (Player.cs:15), independent of the C++ config.
        const float expected_speed = 64.0f * og_magnitude;
        assert(partial_stick.grounded);
        assert(Near(std::sqrt(partial_stick.velocity.x * partial_stick.velocity.x +
                              partial_stick.velocity.z * partial_stick.velocity.z),
                    expected_speed));
    }

    // OG Player.cs:1190-1205 scales Run clip time from grounded horizontal
    // speed; SkinnedModel.Update advances clip time by Rate*dt. Exercise
    // controller state, not only PlayerAnimation, at idle, half MaxSpeed,
    // and MaxSpeed.
    {
        const Room floor = MakeSupportRoom();
        MovementConfig animation_config;
        animation_config.acceleration = 0.0f;
        animation_config.friction = 0.0f;
        PlayerController animation_controller(animation_config);
        const float speeds[] = {0.0f, 32.0f, 64.0f};
        for (float speed : speeds) {
            PlayerState animated;
            animated.position.y = animation_config.player_half_height;
            animated.grounded = true;
            animated.velocity.x = speed;
            PlayerInput animation_input;
            if (speed > 0.0f) animation_input.move = {1.0f, 0.0f};
            animation_controller.Step(animated, animation_input,
                                      camera_forward, kDt, &floor);
            const float expected_rate = speed > 1.0f
                ? 0.1f + std::fmin(std::fmax(speed / (64.0f * 2.0f), 0.0f),
                                   1.0f) * 2.9f
                : 1.0f;
            assert(animated.grounded);
            assert(Near(animated.animation.rate, expected_rate, 0.00001f));
            assert(animated.animation.Count() == 1);
            assert(animated.animation.Current() ==
                   (speed > 1.0f ? PlayerAnimState::Run : PlayerAnimState::Idle));
            assert(Near(animated.animation.At(0).time, expected_rate * kDt,
                        0.00001f));
        }
    }

    // OG Player.cs:1061-1099 saves trueMax before analog scaling, preserves
    // aligned overspeed with PastMaxDeccel, and turns above trueMax at the
    // slower RotateSpeedAboveMax. These one-tick values distinguish source
    // behavior from a plain grounded target-velocity clamp.
    {
        const Room floor = MakeSupportRoom();
        PlayerController overspeed_controller;

        PlayerState aligned_overspeed;
        aligned_overspeed.position.y = config.player_half_height;
        aligned_overspeed.grounded = true;
        aligned_overspeed.velocity = {100.0f, 0.0f, 0.0f};
        aligned_overspeed.target_facing = {1.0f, 0.0f, 0.0f};
        PlayerInput aligned_input;
        aligned_input.move = {1.0f, 0.0f};
        overspeed_controller.Step(aligned_overspeed, aligned_input,
                                  camera_forward, kDt, &floor);
        assert(aligned_overspeed.grounded);
        assert(Near(std::sqrt(aligned_overspeed.velocity.x * aligned_overspeed.velocity.x +
                              aligned_overspeed.velocity.z * aligned_overspeed.velocity.z),
                    99.0f));  // 100 - PastMaxDeccel(60) / 60 Hz

        PlayerState diagonal_turn;
        diagonal_turn.position.y = config.player_half_height;
        diagonal_turn.grounded = true;
        diagonal_turn.velocity = {100.0f, 0.0f, 0.0f};
        diagonal_turn.target_facing = {1.0f, 0.0f, 0.0f};
        PlayerInput diagonal_input;
        diagonal_input.move = {0.70710678f, 0.70710678f};
        overspeed_controller.Step(diagonal_turn, diagonal_input,
                                  camera_forward, kDt, &floor);
        const float og_turn = config.rotate_speed_above_max * kDt;
        assert(diagonal_turn.grounded);
        assert(Near(std::sqrt(diagonal_turn.velocity.x * diagonal_turn.velocity.x +
                              diagonal_turn.velocity.z * diagonal_turn.velocity.z),
                    99.0f));
        assert(Near(diagonal_turn.target_facing.x, std::cos(og_turn), 0.002f));
        assert(Near(diagonal_turn.target_facing.z, std::sin(og_turn), 0.002f));
    }

    // OG Player.cs:1017-1029 computes slope max from the pre-steering
    // targetFacing. Keep the current facing uphill while input turns across
    // the slope; using this tick's input instead changes trueMax from 81.15
    // to 64 and incorrectly selects the above-max deceleration/turn rate.
    {
        PlayerController slope_controller(config);
        PlayerState slope_turn;
        slope_turn.position.y = config.player_half_height;
        slope_turn.grounded = true;
        slope_turn.contact.ground_normal = {0.5f, std::sqrt(0.75f), 0.0f};
        slope_turn.velocity = {0.0f, 0.0f, 70.0f};
        slope_turn.target_facing = {1.0f, 0.0f, 0.0f};
        PlayerInput across_slope;
        across_slope.move = {0.0f, 1.0f};

        slope_controller.BeginMoveTick(slope_turn);
        auto slope_step = slope_controller.TimerInputPhase(
            slope_turn, across_slope, camera_forward, kDt);
        slope_controller.StatePhase(
            slope_turn, across_slope, slope_step, kDt, nullptr);

        const float slope_dot = 1.0f - std::sqrt(0.75f);
        const float og_true_max = 64.0f * (1.0f + 2.0f * slope_dot);
        const float og_speed = 70.0f + std::fmin(
            og_true_max - 70.0f, 500.0f * kDt);
        const float og_turn = 9.424778f * kDt;
        assert(slope_turn.grounded);
        assert(Near(og_true_max, 81.14875f, 0.01f));
        assert(Near(std::sqrt(slope_turn.velocity.x * slope_turn.velocity.x +
                              slope_turn.velocity.z * slope_turn.velocity.z),
                    og_speed));
        assert(Near(slope_turn.target_facing.x, std::cos(og_turn), 0.002f));
        assert(Near(slope_turn.target_facing.z, std::sin(og_turn), 0.002f));
    }

    // OG Player.SetDashSpeed uses a flat horizontal vector on ground; the
    // airborne case above separately checks the normalized upward 3D vector.
    {
        PlayerController grounded_controller;
        const Room floor = MakeSupportRoom();
        PlayerState grounded_dash;
        grounded_dash.position.y = MovementConfig{}.player_half_height;
        grounded_dash.grounded = true;
        grounded_dash.target_facing = {1.0f, 0.0f, 0.0f};
        PlayerInput dash_press;
        dash_press.move = {1.0f, 0.0f};
        dash_press.dash_pressed = true;
        grounded_controller.Step(grounded_dash, dash_press, camera_forward, kDt, &floor);
        assert(grounded_dash.movement_state == PlayerMovementState::Dashing);
        assert(Near(grounded_dash.velocity.x, 140.0f));
        assert(Near(grounded_dash.velocity.y, 0.0f));
    }

    // OG Player.cs:1069-1078 enters Skidding immediately when fast horizontal
    // velocity opposes normalized input past SkidDotThreshold. Keep a real
    // floor under this grounded transition fixture.
    {
        const Room floor = MakeSupportRoom();
        PlayerState skid;
        skid.position.y = config.player_half_height;
        skid.grounded = true;
        skid.velocity = {config.run_speed, 0.0f, 0.0f};
        skid.target_facing = {1.0f, 0.0f, 0.0f};
        PlayerInput reverse;
        reverse.move = {-1.0f, 0.0f};
        reverse.dash_pressed = true;
        controller.Step(skid, reverse, camera_forward, kDt, &floor);
        assert(skid.grounded);
        assert(skid.movement_state == PlayerMovementState::Skidding);
        assert(skid.target_facing.x < 0.0f);
        assert(!reverse.dash_press_consumed);
        // The dash edge stays buffered across the Normal -> Skidding early
        // return, then StSkiddingUpdate accepts it on the following tick.
        controller.Step(skid, reverse, camera_forward, kDt, &floor);
        assert(skid.movement_state == PlayerMovementState::Dashing);
        assert(reverse.dash_press_consumed);
    }

    // OG globally decrements coyote before state dispatch. A timer that reaches
    // exactly zero on this tick cannot be consumed as a coyote jump.
    {
        PlayerState coyote;
        coyote.position.y = 4.0f;
        coyote.coyote_time_remaining = kDt;
        coyote.contact.coyote_height = 8.0f;
        PlayerInput jump_press;
        jump_press.jump_pressed = true;
        jump_press.jump_held = true;
        controller.Step(coyote, jump_press, camera_forward, kDt);
        assert(Near(coyote.coyote_time_remaining, 0.0f));
        assert(!jump_press.jump_press_consumed);
        assert(coyote.velocity.y < 0.0f);
        assert(Near(coyote.position.y, 4.0f));
    }

    // Spring auto-jump still gets half gravity near the apex after the button
    // is released; the source clears autoJump only after leaving that band.
    {
        PlayerState auto_jump;
        auto_jump.position.y = 50.0f;
        auto_jump.velocity.y = 50.0f;
        auto_jump.auto_jump = true;
        controller.Step(auto_jump, {}, camera_forward, kDt);
        assert(auto_jump.auto_jump);
        assert(Near(auto_jump.velocity.y, 45.0f));
        auto_jump.velocity.y = config.half_gravity_threshold + 1.0f;
        controller.Step(auto_jump, {}, camera_forward, kDt);
        assert(!auto_jump.auto_jump);
        assert(Near(auto_jump.velocity.y,
                    config.half_gravity_threshold + 1.0f - config.gravity * kDt));
    }

    // World hitstop is a world-tick gate: it preserves position, velocity,
    // movement timers, and contact-reset count while only consuming hitstop.
    {
        PlayerState hitstop;
        hitstop.position = {2.0f, 30.0f, -4.0f};
        hitstop.velocity = {12.0f, 34.0f, -56.0f};
        hitstop.movement_state = PlayerMovementState::Dashing;
        hitstop.world_hitstop_remaining = 0.02f;
        hitstop.dash_time_remaining = 0.17f;
        hitstop.dash_active_remaining = 0.16f;
        hitstop.no_dash_jump_remaining = 0.08f;
        hitstop.coyote_time_remaining = 0.07f;
        hitstop.hold_jump_time_remaining = 0.06f;
        hitstop.no_move_time_remaining = 0.05f;
        hitstop.platform_carry.time_remaining = 0.04f;
        const uint32_t resets = hitstop.movement_contact_reset_count;
        const Vec3 start = hitstop.position;
        const Vec3 velocity = hitstop.velocity;
        const float dash_time = hitstop.dash_time_remaining;
        const float carry_time = hitstop.platform_carry.time_remaining;
        controller.Step(hitstop, {}, camera_forward, kDt);
        assert(hitstop.position.x == start.x && hitstop.position.y == start.y &&
               hitstop.position.z == start.z);
        assert(hitstop.velocity.x == velocity.x && hitstop.velocity.y == velocity.y &&
               hitstop.velocity.z == velocity.z);
        assert(hitstop.movement_contact_reset_count == resets);
        assert(hitstop.dash_time_remaining == dash_time);
        assert(hitstop.platform_carry.time_remaining == carry_time);
        assert(hitstop.world_hitstop_remaining < 0.02f);
        controller.Step(hitstop, {}, camera_forward, kDt);
        assert(hitstop.position.x == start.x && hitstop.dash_time_remaining == dash_time);
        controller.Step(hitstop, {}, camera_forward, kDt);
        assert(Near(hitstop.dash_time_remaining, dash_time - kDt));
        assert(Near(hitstop.no_dash_jump_remaining, 0.08f - kDt));
    }

    // Expiry runs before dash steering, so collision-reduced 3D momentum is
    // multiplied by .75 instead of being reset to the original DashSpeed.
    {
        PlayerState ending_dash;
        ending_dash.position = {0.0f, 30.0f, 0.0f};
        ending_dash.velocity = {20.0f, 40.0f, -8.0f};
        ending_dash.movement_state = PlayerMovementState::Dashing;
        ending_dash.target_facing = {1.0f, 0.0f, 0.0f};
        ending_dash.last_facing = ending_dash.target_facing;
        ending_dash.dash_time_remaining = kDt;
        ending_dash.dashed_on_ground = false;
        controller.Step(ending_dash, {}, camera_forward, kDt);
        assert(ending_dash.movement_state == PlayerMovementState::Normal);
        assert(Near(ending_dash.velocity.x, 15.0f));
        assert(Near(ending_dash.velocity.y, 30.0f));
        assert(Near(ending_dash.velocity.z, -6.0f));
    }

    // The support phase produces carry before jump launch; the source's
    // strict-positive timer boundary requires the store to survive timers.
    {
        Room platform = MakeSupportRoom();
        platform.moving_surface_count = 1;
        platform.moving_surfaces[0] = {
            .id = 7,
            .displacement = {0.25f, 0.0f, 0.0f},
            .rider_velocity = {5.0f, 20.0f, 0.0f},
            .carry_storage_time = 0.10f,
        };
        PlayerState rider;
        rider.position.y = config.player_half_height;
        rider.grounded = true;
        rider.contact.was_grounded = true;
        rider.contact.coyote_height = rider.position.y;
        rider.coyote_time_remaining = config.coyote_time;
        rider.contact.ground_surface_id = 7;
        PlayerInput jump_press;
        jump_press.jump_pressed = true;
        PlayerController platform_controller(config);
        platform_controller.Step(rider, jump_press, camera_forward, kDt, &platform);
        assert(Near(rider.velocity.x, 5.0f));
        assert(Near(rider.velocity.y, config.jump_speed + 20.0f));
        assert(Near(rider.position.x, 0.25f + 5.0f * kDt));
        assert(Near(rider.platform_carry.time_remaining, 0.0f));
        assert(Near(rider.platform_carry.stored_velocity.x, 0.0f));
    }

    // Spring uses the OG feet anchor (+3) while this engine stores the center,
    // and invoking it during Dash still runs the dash exit cooldown hook.
    {
        PlayerState spring;
        spring.position = {10.0f, config.player_half_height + 10.0f, 0.0f};
        spring.velocity = {80.0f, -20.0f, 0.0f};
        spring.movement_state = PlayerMovementState::Dashing;
        spring.dashes = 0;
        PlayerController spring_controller(config);
        spring_controller.Spring(spring, {0.0f, 0.0f, 0.0f});
        assert(spring.movement_state == PlayerMovementState::Normal);
        assert(Near(spring.position.y, config.player_half_height + 8.0f));
        assert(Near(spring.position.x, 6.0f));
        assert(Near(spring.dash_cooldown_remaining, config.dash_cooldown));
        assert(Near(spring.velocity.y, config.spring_jump_speed));
        assert(spring.auto_jump && spring.dashes == 1);
    }

    return 0;
}
