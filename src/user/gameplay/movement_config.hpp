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
};

}  // namespace madeline_cube

