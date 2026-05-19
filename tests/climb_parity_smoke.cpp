#include <cassert>

#include "../src/user/gameplay/player/player_controller.hpp"

using namespace madeline_cube;

int main() {
    PlayerController controller;
    const Vec3 camera_forward = {0.0f, 0.0f, 1.0f};

    PlayerState climb;
    climb.wall_left = true;
    climb.wall_normal = {1.0f, 0.0f, 0.0f};  // wall on left, normal points right
    climb.last_facing = {-1.0f, 0.0f, 0.0f};  // facing left into wall
    climb.wall_climbable = true;
    PlayerInput grab;
    grab.climb_held = true;
    controller.Step(climb, grab, camera_forward, 1.0f / 60.0f);
    assert(climb.movement_state == PlayerMovementState::Climbing);

    grab.move = {0.0f, 1.0f};
    controller.Step(climb, grab, camera_forward, 1.0f / 60.0f);
    assert(climb.velocity.y > 0.0f);

    PlayerState wall_jump;
    wall_jump.wall_left = true;
    wall_jump.wall_normal = {1.0f, 0.0f, 0.0f};  // wall on left, normal points right
    wall_jump.wall_climbable = true;
    PlayerInput jump;
    jump.jump_pressed = true;
    controller.Step(wall_jump, jump, camera_forward, 1.0f / 60.0f);
    assert(wall_jump.velocity.x > 0.0f);
    assert(wall_jump.velocity.y > 0.0f);

    return 0;
}
