#pragma once

namespace madeline_cube {

struct MovementConfig {
    // Celeste64 values scaled to the prototype's smaller graybox units.
    float run_speed = 6.4f;
    float acceleration = 50.0f;
    float past_max_deceleration = 6.0f;
    float air_accel_mult_min = 0.5f;
    float air_accel_mult_max = 1.0f;
    float rotate_threshold = 1.28f;
    float rotate_speed = 9.424778f;
    float rotate_speed_above_max = 3.769911f;
    float friction = 80.0f;
    float air_friction_mult = 0.1f;
    float gravity = 60.0f;
    float max_fall_speed = -12.0f;
    float half_gravity_threshold = 10.0f;
    float jump_speed = 9.0f;
    float jump_hold_time = 0.10f;
    float jump_xy_boost = 1.0f;
    float dash_speed = 14.0f;
    float dash_end_speed_multiplier = 0.75f;
    float dash_duration = 0.20f;
    float dash_reset_cooldown = 0.20f;
    float dash_cooldown = 0.10f;
    float dash_rotate_speed = 1.884956f;
    float dash_jump_speed = 4.0f;
    float dash_jump_hold_speed = 2.0f;
    float dash_jump_hold_time = 0.30f;
    float dash_jump_xy_boost = 1.6f;
    float skid_dot_threshold = -0.7f;
    float skidding_start_acceleration = 30.0f;
    float skidding_acceleration = 50.0f;
    float end_skid_speed = 5.12f;
    float skid_jump_speed = 12.0f;
    float skid_jump_hold_time = 0.16f;
    float skid_jump_xy_speed = 8.96f;
    float respawn_fall_height = -20.0f;

    float coyote_time = 0.12f;           // seconds after leaving ground where jump still works
    float jump_buffer_time = 0.08f;      // seconds before landing where jump is queued
    float wall_grab_time = 1.5f;         // max seconds to hold a wall
    float wall_slide_speed = 3.0f;       // downward speed while wall-grabbing
    float wall_jump_speed_x = 8.0f;      // horizontal speed off a wall
    float wall_jump_speed_y = 9.0f;      // vertical speed off a wall
    float wall_jump_cooldown = 0.15f;    // seconds before another wall interaction
    float climb_speed = 4.0f;
    float climb_hop_up_speed = 8.0f;
    float climb_hop_forward_speed = 4.0f;
};

}  // namespace madeline_cube
