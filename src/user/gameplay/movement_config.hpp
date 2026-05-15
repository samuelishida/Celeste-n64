#pragma once

namespace madeline_cube {

struct MovementConfig {
    float run_speed = 6.0f;
    float ground_acceleration = 36.0f;
    float air_acceleration = 18.0f;
    float ground_friction = 28.0f;
    float gravity = 24.0f;
    float jump_speed = 9.0f;
    float dash_speed = 14.0f;
    float dash_duration = 0.16f;
    float respawn_fall_height = -20.0f;

    // Milestone 1 additions
    float coyote_time = 0.08f;           // seconds after leaving ground where jump still works
    float jump_buffer_time = 0.08f;      // seconds before landing where jump is queued
    float variable_jump_cut = 0.5f;      // multiplier on upward velocity when jump released early
    float wall_grab_time = 1.5f;         // max seconds to hold a wall
    float wall_slide_speed = 3.0f;       // downward speed while wall-grabbing
    float wall_jump_speed_x = 8.0f;      // horizontal speed off a wall
    float wall_jump_speed_y = 9.0f;      // vertical speed off a wall
    float wall_jump_cooldown = 0.15f;    // seconds before another wall interaction
};

}  // namespace madeline_cube

