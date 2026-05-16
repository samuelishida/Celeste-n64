#pragma once

#include <cstdint>

#include "math_types.hpp"

namespace madeline_cube {

struct PlayerInput {
    Vec2 move;
    bool jump_pressed = false;
    bool jump_held = false;
    bool dash_pressed = false;
};

enum class PlayerMovementState : uint8_t {
    Normal = 0,
    Dashing,
    Skidding,
};

struct PlayerState {
    Vec3 position;
    Vec3 velocity;
    Vec3 facing = {0.0f, 0.0f, 1.0f};
    Vec3 target_facing = {0.0f, 0.0f, 1.0f};
    Vec3 last_facing = {0.0f, 0.0f, 1.0f};
    bool grounded = false;
    bool air_dash_available = true;
    float dash_time_remaining = 0.0f;
    float dash_cooldown_remaining = 0.0f;
    float dash_reset_cooldown_remaining = 0.0f;
    float no_dash_jump_remaining = 0.0f;
    bool dashed_on_ground = false;
    PlayerMovementState movement_state = PlayerMovementState::Normal;

    // Coyote time: counts down after leaving ground.
    float coyote_time_remaining = 0.0f;

    // Jump buffer: counts down after jump pressed in air.
    float jump_buffer_remaining = 0.0f;

    // Wall grab state
    bool wall_grabbing = false;
    float wall_grab_time_remaining = 0.0f;
    float wall_jump_cooldown_remaining = 0.0f;

    // Celeste-like jump sustain.
    float hold_jump_time_remaining = 0.0f;
    float hold_jump_speed = 0.0f;
    bool auto_jump = false;

    // Skid state.
    float no_skid_jump_remaining = 0.0f;

    // Collision probes (set by caller before Step)
    bool wall_left = false;
    bool wall_right = false;
};

}  // namespace madeline_cube
