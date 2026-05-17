#include <cassert>
#include <cmath>

#include "../src/user/gameplay/player/player_controller.hpp"

using namespace madeline_cube;

namespace {
constexpr float kDt = 1.0f / 60.0f;
bool Near(float a, float b, float eps = 0.02f) { return std::fabs(a - b) <= eps; }
}

int main() {
    MovementConfig config;
    PlayerController controller(config);
    const Vec3 camera_forward = {0.0f, 0.0f, 1.0f};

    PlayerState jump;
    jump.grounded = true;
    PlayerInput held_jump;
    held_jump.jump_pressed = true;
    held_jump.jump_held = true;
    controller.Step(jump, held_jump, camera_forward, kDt);
    assert(Near(jump.velocity.y, config.jump_speed));
    controller.Step(jump, {.jump_held = true}, camera_forward, kDt);
    assert(jump.velocity.y < config.jump_speed);
    assert(jump.velocity.y > 0.0f);

    PlayerState dash;
    dash.grounded = false;
    PlayerInput air_dash;
    air_dash.move = {1.0f, 0.0f};
    air_dash.dash_pressed = true;
    controller.Step(dash, air_dash, camera_forward, kDt);
    assert(dash.movement_state == PlayerMovementState::Dashing);
    assert(dash.velocity.x == 0.0f);

    PlayerState skid;
    skid.grounded = true;
    skid.velocity = {config.run_speed, 0.0f, 0.0f};
    skid.target_facing = {1.0f, 0.0f, 0.0f};
    PlayerInput reverse;
    reverse.move = {-1.0f, 0.0f};
    controller.Step(skid, reverse, camera_forward, kDt);
    assert(skid.movement_state == PlayerMovementState::Normal);
    assert(skid.velocity.x < config.run_speed);

    return 0;
}
