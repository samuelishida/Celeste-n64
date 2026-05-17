#include <cassert>
#include <cmath>

#include "../../src/user/gameplay/player/player_controller.hpp"

using namespace madeline_cube;

int main() {
    PlayerController controller;
    constexpr float dt = 1.0f / 60.0f;

    PlayerState hold;
    hold.movement_state = PlayerMovementState::Climbing;
    hold.stamina = 100.0f;
    controller.Step(hold, {.climb_held = true}, {0,0,1}, dt);
    const float hold_drain = 100.0f - hold.stamina;

    PlayerState up;
    up.movement_state = PlayerMovementState::Climbing;
    up.stamina = 100.0f;
    controller.Step(up, {.move = {0,1}, .climb_held = true}, {0,0,1}, dt);
    const float up_drain = 100.0f - up.stamina;
    assert(up_drain > hold_drain);

    PlayerState neutral;
    neutral.movement_state = PlayerMovementState::Climbing;
    neutral.target_facing = {1,0,0};
    neutral.stamina = 100.0f;
    controller.Step(neutral, {.jump_pressed = true, .climb_held = true}, {0,0,1}, dt);

    PlayerState directional;
    directional.movement_state = PlayerMovementState::Climbing;
    directional.target_facing = {1,0,0};
    directional.stamina = 100.0f;
    controller.Step(directional, {.move = {-1,0}, .jump_pressed = true, .climb_held = true}, {0,0,1}, dt);
    assert(neutral.velocity.y > directional.velocity.y);
    assert(std::abs(neutral.velocity.x) < std::abs(directional.velocity.x));

    PlayerState exhausted;
    exhausted.movement_state = PlayerMovementState::Climbing;
    exhausted.stamina = 0.0f;
    controller.Step(exhausted, {.climb_held = true}, {0,0,1}, dt);
    assert(exhausted.locomotion_state == LocomotionState::Fall);
    assert(exhausted.climb_exhausted);
    exhausted.wall_left = true;
    controller.Step(exhausted, {.climb_held = true}, {0,0,1}, dt);
    assert(exhausted.movement_state != PlayerMovementState::Climbing);
    exhausted.grounded = true;
    controller.Step(exhausted, {}, {0,0,1}, dt);
    assert(!exhausted.climb_exhausted);
    return 0;
}
