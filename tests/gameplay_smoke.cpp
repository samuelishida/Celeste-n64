#include <cassert>

#include "../src/user/gameplay/collectible.hpp"
#include "../src/user/gameplay/player_controller.hpp"
#include "../src/user/gameplay/respawn_system.hpp"

using namespace madeline_cube;

int main() {
    MovementConfig config;
    PlayerController controller(config);
    RespawnSystem respawn(config);

    // --- Basic jump ---
    {
        PlayerState player;
        player.grounded = true;

        PlayerInput jump_input;
        jump_input.jump_pressed = true;
        jump_input.jump_held = true;
        controller.Step(player, jump_input, 1.0f / 60.0f);
        assert(!player.grounded);
        assert(player.velocity.y > 0.0f);
    }

    // --- Air dash consumption ---
    {
        PlayerState player;
        player.grounded = false;
        player.air_dash_available = true;

        PlayerInput dash_input;
        dash_input.move = {1.0f, 0.0f};
        dash_input.dash_pressed = true;
        controller.Step(player, dash_input, 1.0f / 60.0f);
        assert(!player.air_dash_available);
        assert(player.velocity.x > 0.0f);
    }

    // --- Coyote time ---
    {
        PlayerState player;
        player.grounded = false;
        player.coyote_time_remaining = config.coyote_time;
        player.velocity.y = -1.0f;

        PlayerInput jump_input;
        jump_input.jump_pressed = true;
        jump_input.jump_held = true;
        controller.Step(player, jump_input, 1.0f / 60.0f);
        assert(player.velocity.y > 0.0f);  // jumped despite not grounded
    }

    // --- Jump buffering ---
    {
        PlayerState player;
        player.grounded = false;
        player.velocity.y = -1.0f;

        PlayerInput jump_input;
        jump_input.jump_pressed = true;
        jump_input.jump_held = true;
        controller.Step(player, jump_input, 1.0f / 60.0f);  // buffer starts
        assert(player.jump_buffer_remaining > 0.0f);

        // Now land
        player.grounded = true;
        controller.Step(player, jump_input, 1.0f / 60.0f);
        assert(player.velocity.y > 0.0f);  // buffered jump executed
    }

    // --- Variable jump height ---
    {
        PlayerState player;
        player.grounded = true;

        PlayerInput jump_input;
        jump_input.jump_pressed = true;
        jump_input.jump_held = true;
        controller.Step(player, jump_input, 1.0f / 60.0f);
        float full_jump_vel = player.velocity.y;

        PlayerState player2;
        player2.grounded = true;
        controller.Step(player2, jump_input, 1.0f / 60.0f);
        // Release jump immediately
        PlayerInput release_input;
        release_input.jump_held = false;
        controller.Step(player2, release_input, 1.0f / 60.0f);
        assert(player2.velocity.y < full_jump_vel);  // cut short
    }

    // --- Wall grab ---
    {
        PlayerState player;
        player.grounded = false;
        player.velocity.y = -1.0f;
        player.wall_left = true;
        player.wall_grab_time_remaining = config.wall_grab_time;

        PlayerInput grab_input;
        grab_input.jump_held = true;
        controller.Step(player, grab_input, 1.0f / 60.0f);
        assert(player.wall_grabbing);
        assert(player.velocity.y == -config.wall_slide_speed);
    }

    // --- Wall jump ---
    {
        PlayerState player;
        player.grounded = false;
        player.velocity.y = -1.0f;
        player.wall_left = true;
        player.wall_grab_time_remaining = config.wall_grab_time;

        PlayerInput grab_input;
        grab_input.jump_held = true;
        controller.Step(player, grab_input, 1.0f / 60.0f);
        assert(player.wall_grabbing);

        // Press jump again to wall-jump
        PlayerInput wall_jump_input;
        wall_jump_input.jump_pressed = true;
        wall_jump_input.jump_held = true;
        controller.Step(player, wall_jump_input, 1.0f / 60.0f);
        assert(!player.wall_grabbing);
        assert(player.velocity.x > 0.0f);  // pushed away from wall
        assert(player.velocity.y > 0.0f);  // upward
    }

    // --- Collectible pickup ---
    {
        CollectibleState collectible;
        collectible.position = {0.0f, 0.0f, 0.0f};
        PlayerState player;
        player.position = collectible.position;
        assert(TryCollect(collectible, player.position));
        assert(collectible.collected);
    }

    // --- Kill-plane respawn ---
    {
        PlayerState player;
        player.position.y = config.respawn_fall_height - 1.0f;
        const Vec3 checkpoint = {0.0f, 2.0f, 0.0f};
        assert(respawn.Step(player, checkpoint));
        assert(player.position.y == checkpoint.y);
        assert(player.velocity.x == 0.0f);
        assert(player.air_dash_available);
    }

    return 0;
}
