#pragma once

namespace madeline_cube {

struct MovementConfig {
    // OG Celeste64 values (Y-up, no rescaling — these ARE the original values).
    float run_speed = 64.0f;
    float acceleration = 500.0f;
    float past_max_deceleration = 60.0f;
    float air_accel_mult_min = 0.5f;
    float air_accel_mult_max = 1.0f;
    float friction = 800.0f;
    float air_friction_mult = 0.1f;
    float gravity = 600.0f;
    float max_fall_speed = -120.0f;
    float half_gravity_threshold = 100.0f;
    float jump_speed = 90.0f;
    float jump_hold_time = 0.10f;
    float jump_xy_boost = 10.0f;
    float dash_speed = 140.0f;
    float dash_end_speed_multiplier = 0.75f;
    float dash_duration = 0.20f;
    float dash_reset_cooldown = 0.20f;
    float dash_cooldown = 0.10f;
    float dash_jump_speed = 40.0f;
    float dash_jump_hold_speed = 20.0f;
    float dash_jump_hold_time = 0.30f;
    float dash_jump_xy_boost = 16.0f;
    float respawn_fall_height = -200.0f;

    float wall_grab_time = 1.5f;         // max seconds to hold a wall
    float wall_slide_speed = 16.0f;      // downward speed while wall-sliding (OG gravity-based)
    float wall_jump_speed_x = 83.2f;    // WallJumpXYSpeed = MaxSpeed * 1.3
    float wall_jump_speed_y = 90.0f;    // same as JumpSpeed
    float wall_jump_cooldown = 0.15f;    // seconds before another wall interaction
    float climb_speed = 40.0f;
    float climb_hop_up_speed = 80.0f;
    float climb_hop_forward_speed = 40.0f;

    // Skid state (OG Celeste64 §16): running fast and pushing opposite.
    float skid_dot_threshold = -0.7f;
    float skid_start_accel = 300.0f;
    float skid_accel = 500.0f;
    float end_skid_speed = 51.2f;  // MaxSpeed * 0.8

    // Slope speed multiplier (§42): scale ground speed by ground_normal.
    float slope_downhill_mult = 1.25f;
    float slope_uphill_mult = 0.75f;

    // Ice (§): low-friction ground multiplier.
    float ice_friction_mult = 0.1f;

    // Stamina drain rates (OG Celeste64 values)
    float stamina_hold_drain = 10.0f;      // per second while holding wall (idle)
    float stamina_up_drain = 20.0f;        // per second while climbing upward
    float stamina_jump_cost = 25.0f;       // flat cost per climb jump
    float climb_hop_no_move_time = 0.25f;  // no-move time after ledge hop
    float climb_cooldown = 0.30f;          // cooldown after leaving climb before re-enter
};

struct MovementProfile {
    float run_max_speed = 64.0f;
    float ground_acceleration = 500.0f;
    float ground_deceleration = 800.0f;
    float air_acceleration = 500.0f;
    float air_turn_acceleration = 500.0f;
    float past_max_decel = 60.0f;
    float rotate_threshold = 12.8f;
    float rotate_speed = 9.424778f;  // Tau * 1.5
    float rotate_speed_above_max = 3.769911f;  // Tau * 0.6
    float dash_rotate_speed = 1.884956f;     // Tau * 0.3
    float coyote_time = 0.12f;
    float jump_buffer_time = 0.08f;

    float dash_speed = 140.0f;
    float dash_hitstop_time = 0.02f;
    float dash_active_time = 0.2f;
    float dash_cooldown = 0.10f;
    float dash_long_jump_speed = 100.0f;

    float wall_jump_horizontal_speed = 83.2f;
    float wall_jump_vertical_speed = 90.0f;
    float neutral_wall_jump_horizontal_speed = 40.0f;
    float neutral_wall_jump_vertical_speed = 105.0f;

    float climb_speed = 40.0f;
};

// Derive a MovementProfile from a MovementConfig. Only fields that have a
// MovementConfig source are overridden; profile-only fields keep their
// hardcoded defaults. MovementConfig is the source of truth for tuning.
inline MovementProfile ToProfile(const MovementConfig& config) {
    MovementProfile profile;
    profile.run_max_speed = config.run_speed;
    profile.ground_acceleration = config.acceleration;
    profile.air_acceleration = config.acceleration;
    profile.air_turn_acceleration = config.acceleration;
    profile.ground_deceleration = config.friction;
    profile.past_max_decel = config.past_max_deceleration;
    profile.dash_speed = config.dash_speed;
    profile.dash_active_time = config.dash_duration;
    profile.dash_cooldown = config.dash_cooldown;
    profile.dash_long_jump_speed = config.dash_jump_speed;
    profile.wall_jump_horizontal_speed = config.wall_jump_speed_x;
    profile.wall_jump_vertical_speed = config.wall_jump_speed_y;
    profile.climb_speed = config.climb_speed;
    return profile;
}

}  // namespace madeline_cube
