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

struct MovementProfile {
    float run_max_speed = 6.4f;
    float ground_acceleration = 120.0f;
    float ground_deceleration = 180.0f;
    float air_acceleration = 24.0f;
    float air_turn_acceleration = 36.0f;

    float rise_gravity = 42.0f;
    float apex_gravity = 84.0f;
    float fall_gravity = 96.0f;
    float max_fall_speed = -14.0f;
    float jump_speed = 9.0f;
    float jump_cut_multiplier = 0.5f;
    float apex_threshold = 1.0f;
    float coyote_time = 0.15f;
    float jump_buffer_time = 0.10f;

    float dash_speed = 14.0f;
    float dash_hitstop_time = 0.05f;
    float dash_active_time = 0.18f;
    float dash_cooldown = 0.10f;
    float dash_long_jump_speed = 10.0f;

    float wall_jump_horizontal_speed = 8.0f;
    float wall_jump_vertical_speed = 9.0f;
    float neutral_wall_jump_horizontal_speed = 4.0f;
    float neutral_wall_jump_vertical_speed = 10.5f;

    float stamina_max = 110.0f;
    float climb_hold_drain_per_second = 10.0f;
    float climb_up_drain_per_second = 25.0f;
    float climb_jump_cost = 27.5f;
    float climb_speed = 4.0f;
    float exhausted_slide_speed = 4.0f;
};

}  // namespace madeline_cube
