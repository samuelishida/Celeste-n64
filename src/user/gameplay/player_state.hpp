#pragma once

#include "math_types.hpp"

namespace madeline_cube {

struct PlayerInput {
    Vec2 move;
    bool jump_pressed = false;
    bool dash_pressed = false;
};

struct PlayerState {
    Vec3 position;
    Vec3 velocity;
    Vec3 last_facing = {0.0f, 0.0f, 1.0f};
    bool grounded = false;
    bool air_dash_available = true;
    float dash_time_remaining = 0.0f;
};

}  // namespace madeline_cube

