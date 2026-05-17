#include <cassert>
#include <cmath>

#include "../../src/user/gameplay/player/player_controller.hpp"

using namespace madeline_cube;

int main() {
    PlayerController controller;
    constexpr float dt = 1.0f / 60.0f;
    PlayerState dash;
    dash.grounded = false;
    PlayerInput input;
    input.move = {1.0f, 0.0f};
    input.dash_pressed = true;
    controller.Step(dash, input, {0.0f, 0.0f, 1.0f}, dt);
    assert(dash.locomotion_state == LocomotionState::Dash);
    assert(dash.velocity.x == 0.0f && dash.velocity.y == 0.0f);
    controller.Step(dash, {}, {0.0f, 0.0f, 1.0f}, dt);
    controller.Step(dash, {}, {0.0f, 0.0f, 1.0f}, dt);
    controller.Step(dash, {}, {0.0f, 0.0f, 1.0f}, dt);
    assert(dash.velocity.x > 0.0f);
    assert(std::fabs(dash.velocity.y) < 0.001f);

    PlayerState long_jump;
    long_jump.grounded = true;
    long_jump.dashed_on_ground = true;
    long_jump.movement_state = PlayerMovementState::Dashing;
    long_jump.dash_time_remaining = 0.05f;
    long_jump.dash_active_remaining = 0.05f;
    long_jump.coyote_time_remaining = 0.15f;
    long_jump.no_dash_jump_remaining = 0.0f;
    long_jump.target_facing = {1.0f, 0.0f, 0.0f};
    controller.Step(long_jump, {.jump_pressed = true, .jump_held = true}, {0,0,1}, dt);
    assert(long_jump.velocity.x > 0.0f);
    assert(long_jump.velocity.y > 0.0f);
    return 0;
}
