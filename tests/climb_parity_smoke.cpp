#include <cassert>
#include <cmath>

#include "../src/user/gameplay/player/player_controller.hpp"

using namespace madeline_cube;

int main() {
    PlayerController controller;
    // Source ClimbCheckAt accepts relative input only neutral or into the wall.
    const Vec3 camera_forward = {-1.0f, 0.0f, 0.0f};

    Room wall_room;
    wall_room.colliders[wall_room.collider_count++] = {
        .type = ColliderType::Box,
        .bounds = {.min = {-6, -100, -100}, .max = {-3, 100, 100}},
        .solid = true,
        .face_id = 1,
    };
    PlayerState climb;
    climb.position = {0, 30, 0};
    climb.wall_contact = true;
    climb.wall_normal = {1.0f, 0.0f, 0.0f};  // wall on left, normal points right
    climb.target_facing = climb.last_facing = {-1.0f, 0.0f, 0.0f};  // facing left into wall
    PlayerInput grab;
    grab.climb_held = true;
    controller.Step(climb, grab, camera_forward, 1.0f / 60.0f, &wall_room);
    assert(climb.movement_state == PlayerMovementState::Climbing);

    grab.move = {0.0f, 1.0f};
    const float before_y = climb.position.y;
    controller.Step(climb, grab, camera_forward, 1.0f / 60.0f, &wall_room);
    assert(climb.position.y > before_y && climb.velocity.y == 0);

    PlayerState wall_jump;
    wall_jump.position = {0, 30, 0};
    wall_jump.wall_contact = true;
    wall_jump.wall_normal = {1.0f, 0.0f, 0.0f};  // wall on left, normal points right
    wall_jump.last_facing = {-1.0f, 0.0f, 0.0f};
    wall_jump.platform_carry.stored_velocity = {10.0f, 20.0f, 0.0f};
    wall_jump.platform_carry.time_remaining = 0.10f;
    PlayerInput jump;
    jump.jump_pressed = true;
    controller.Step(wall_jump, jump, camera_forward, 1.0f / 60.0f, &wall_room);
    // OG Player.WallJump (Player.cs:748-758) SETS horizontal launch to
    // targetFacing * WallJumpXYSpeed and vertical launch to JumpSpeed.
    assert(std::fabs(wall_jump.velocity.x - 93.2f) < 0.02f);  // launch + platform carry
    assert(std::fabs(wall_jump.velocity.y - 110.0f) < 0.02f); // launch + platform carry
    assert(wall_jump.platform_carry.time_remaining == 0.0f);

    // climb-parity-3 Inc 2: Normal-state directional walljump (room == nullptr
    // shim). The fresh waist-query path needs a real room; the shim keeps the
    // flag-based SET-away launch for controller-only fixtures. Wall on the
    // left (+x normal), facing left (into the wall) → jump must SET velocity
    // toward +x (away from the wall), not additive.
    {
        PlayerState njump;
        njump.position = {0, 30, 0};
        njump.wall_contact = true;
        njump.wall_normal = {1.0f, 0.0f, 0.0f};
        njump.last_facing = {-1.0f, 0.0f, 0.0f};
        njump.grounded = false;
        PlayerInput njump_in;
        njump_in.jump_pressed = true;
        controller.Step(njump, njump_in, camera_forward, 1.0f / 60.0f, &wall_room);
        assert(njump.velocity.x > 0.0f);   // SET away from the wall
        assert(njump.velocity.y > 0.0f);   // wall_jump_speed_y
        assert(njump.target_facing.x > 0.0f);
    }

    return 0;
}
