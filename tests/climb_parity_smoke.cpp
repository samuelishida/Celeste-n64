#include <cassert>

#include "../src/user/gameplay/player/player_controller.hpp"

using namespace madeline_cube;

int main() {
    PlayerController controller;
    const Vec3 camera_forward = {0.0f, 0.0f, 1.0f};

    PlayerState climb;
    climb.wall_contact = true;
    climb.wall_normal = {1.0f, 0.0f, 0.0f};  // wall on left, normal points right
    climb.last_facing = {-1.0f, 0.0f, 0.0f};  // facing left into wall
    PlayerInput grab;
    grab.climb_held = true;
    controller.Step(climb, grab, camera_forward, 1.0f / 60.0f);
    assert(climb.movement_state == PlayerMovementState::Climbing);

    grab.move = {0.0f, 1.0f};
    controller.Step(climb, grab, camera_forward, 1.0f / 60.0f);
    assert(climb.velocity.y > 0.0f);

    PlayerState wall_jump;
    wall_jump.wall_contact = true;
    wall_jump.wall_normal = {1.0f, 0.0f, 0.0f};  // wall on left, normal points right
    wall_jump.last_facing = {-1.0f, 0.0f, 0.0f};
    PlayerInput jump;
    jump.jump_pressed = true;
    controller.Step(wall_jump, jump, camera_forward, 1.0f / 60.0f);
    assert(wall_jump.velocity.x > 0.0f);
    assert(wall_jump.velocity.y > 0.0f);

    // climb-parity-3 Inc 2: Normal-state directional walljump (room == nullptr
    // shim). The fresh waist-query path needs a real room; the shim keeps the
    // flag-based SET-away launch for controller-only fixtures. Wall on the
    // left (+x normal), facing left (into the wall) → jump must SET velocity
    // toward +x (away from the wall), not additive.
    {
        PlayerState njump;
        njump.wall_contact = true;
        njump.wall_normal = {1.0f, 0.0f, 0.0f};
        njump.last_facing = {-1.0f, 0.0f, 0.0f};
        njump.grounded = false;
        PlayerInput njump_in;
        njump_in.jump_pressed = true;
        controller.Step(njump, njump_in, camera_forward, 1.0f / 60.0f);
        assert(njump.velocity.x > 0.0f);   // SET away from the wall
        assert(njump.velocity.y > 0.0f);   // wall_jump_speed_y
        assert(njump.target_facing.x > 0.0f);
    }

    return 0;
}
