#pragma once

#include <cstdint>

#include "gameplay/math_types.hpp"

namespace madeline_cube {

struct PlayerInput {
    Vec2 move;
    bool jump_pressed = false;
    bool jump_held = false;
    bool dash_pressed = false;
    bool climb_held = false;
};

enum class PlayerMovementState : uint8_t {
    Normal = 0,
    Dashing,
    Climbing,
};

enum class LocomotionState : uint8_t {
    Idle = 0,
    Run,
    Jump,
    Dash,
    Climb,
    Fall,
};

struct ContactState {
    Vec3 ground_normal = {0.0f, 1.0f, 0.0f};
    float coyote_height = 0.0f;
    Vec3 previous_velocity;
    float ground_snap_cooldown_remaining = 0.0f;
    bool was_grounded = false;
};

struct PlatformCarryState {
    Vec3 stored_velocity;
    float time_remaining = 0.0f;
};

struct ClimbState {
    Vec3 wall_normal;
    int16_t wall_surface_id = -1;
    Vec3 corner_from;
    Vec3 corner_to;
    float corner_progress = 0.0f;
    float cooldown_remaining = 0.0f;
    float hop_no_move_remaining = 0.0f;
};

struct PlayerState {
    Vec3 position = {};
    Vec3 prev_position = {};
    Vec3 velocity = {};
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
    LocomotionState locomotion_state = LocomotionState::Idle;
    float dash_hitstop_remaining = 0.0f;
    float dash_active_remaining = 0.0f;

    // Coyote time: counts down after leaving ground.
    float coyote_time_remaining = 0.0f;

    // Jump buffer: counts down after jump pressed in air.
    float jump_buffer_remaining = 0.0f;

    // Wall grab state
    bool wall_grabbing = false;
    float wall_grab_time_remaining = 0.0f;
    float wall_jump_cooldown_remaining = 0.0f;

    // Stamina system (OG Celeste64-style)
    float stamina = 110.0f;
    float stamina_max = 110.0f;
    bool climb_exhausted = false;

    // Celeste-like jump sustain.
    float hold_jump_time_remaining = 0.0f;
    float hold_jump_speed = 0.0f;
    bool auto_jump = false;

    // Movement timer.
    float no_move_time_remaining = 0.0f;

    // Source bookkeeping retained explicitly for the later motor migration.
    int8_t dash_count = 1;
    ContactState contact;
    PlatformCarryState platform_carry;
    ClimbState climb;

    // Collision probes (set by caller before Step)
    bool wall_left = false;
    bool wall_right = false;
    // True if the nearest wall face permits climbing (legacy: any wall; collmesh: MAT_CLIMBABLE).
    bool wall_climbable = false;
    // Wall normal from the most recent wall contact (set by motor or test fixture).
    Vec3 wall_normal = {0.0f, 0.0f, 0.0f};

    Vec3 InterpolatedPosition(float alpha) const {
        return {
            prev_position.x + (position.x - prev_position.x) * alpha,
            prev_position.y + (position.y - prev_position.y) * alpha,
            prev_position.z + (position.z - prev_position.z) * alpha,
        };
    }
};

}  // namespace madeline_cube
