#include <cassert>
#include <cmath>

#include "../src/user/gameplay/actor/actor.hpp"
#include "../src/user/gameplay/player/camera_controller.hpp"
#include "../src/user/gameplay/world/collectible.hpp"
#include "../src/user/gameplay/player/player_controller.hpp"
#include "../src/user/gameplay/actor/refill_actor.hpp"
#include "../src/user/gameplay/world/respawn_system.hpp"
#include "../src/user/gameplay/actor/spring_actor.hpp"
#include "../src/user/gameplay/actor/strawberry_actor.hpp"

using namespace madeline_cube;

int main() {
    MovementConfig config;
    PlayerController controller(config);
    RespawnSystem respawn(config);
    const Vec3 camera_forward = {0.0f, 0.0f, 1.0f};

    // --- Basic jump ---
    {
        PlayerState player;
        player.grounded = true;

        PlayerInput jump_input;
        jump_input.jump_pressed = true;
        jump_input.jump_held = true;
        controller.Step(player, jump_input, camera_forward, 1.0f / 60.0f);
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
        controller.Step(player, dash_input, camera_forward, 1.0f / 60.0f);
        assert(!player.air_dash_available);
        assert(player.velocity.x > 0.0f);
        assert(player.velocity.y > 0.0f);
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
        controller.Step(player, jump_input, camera_forward, 1.0f / 60.0f);
        assert(player.velocity.y > 0.0f);
    }

    // --- Jump buffering ---
    {
        PlayerState player;
        player.grounded = false;
        player.velocity.y = -1.0f;

        PlayerInput jump_input;
        jump_input.jump_pressed = true;
        jump_input.jump_held = true;
        controller.Step(player, jump_input, camera_forward, 1.0f / 60.0f);
        assert(player.jump_buffer_remaining > 0.0f);

        player.grounded = true;
        controller.Step(player, jump_input, camera_forward, 1.0f / 60.0f);
        assert(player.velocity.y > 0.0f);
    }

    // --- Jump sustain release ---
    {
        PlayerState player;
        player.grounded = true;

        PlayerInput jump_input;
        jump_input.jump_pressed = true;
        jump_input.jump_held = true;
        controller.Step(player, jump_input, camera_forward, 1.0f / 60.0f);
        const float held_jump_velocity = player.velocity.y;

        PlayerInput release_input;
        release_input.jump_held = false;
        controller.Step(player, release_input, camera_forward, 1.0f / 60.0f);
        assert(player.velocity.y < held_jump_velocity);
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
        controller.Step(player, grab_input, camera_forward, 1.0f / 60.0f);
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
        controller.Step(player, grab_input, camera_forward, 1.0f / 60.0f);
        assert(player.wall_grabbing);

        PlayerInput wall_jump_input;
        wall_jump_input.jump_pressed = true;
        wall_jump_input.jump_held = true;
        controller.Step(player, wall_jump_input, camera_forward, 1.0f / 60.0f);
        assert(!player.wall_grabbing);
        assert(player.velocity.x > 0.0f);
        assert(player.velocity.y > 0.0f);
    }

    // --- Camera-relative movement ---
    {
        PlayerState player;
        player.grounded = true;

        PlayerInput move_input;
        move_input.move = {0.0f, 1.0f};
        controller.Step(player, move_input, {1.0f, 0.0f, 0.0f}, 1.0f / 60.0f);
        assert(player.velocity.x > 0.0f);
        assert(std::fabs(player.velocity.z) < 0.1f);
    }

    // --- Grounded dash and skid entry ---
    {
        PlayerState player;
        player.grounded = true;
        player.velocity = {config.run_speed, 0.0f, 0.0f};
        player.target_facing = {1.0f, 0.0f, 0.0f};

        PlayerInput reverse_input;
        reverse_input.move = {-1.0f, 0.0f};
        controller.Step(player, reverse_input, camera_forward, 1.0f / 60.0f);
        assert(player.movement_state == PlayerMovementState::Skidding);

        PlayerInput dash_input;
        dash_input.dash_pressed = true;
        controller.Step(player, dash_input, camera_forward, 1.0f / 60.0f);
        assert(player.movement_state == PlayerMovementState::Dashing);
        assert(player.dashed_on_ground);
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
        player.movement_state = PlayerMovementState::Dashing;
        const Vec3 checkpoint = {0.0f, 2.0f, 0.0f};
        assert(respawn.Step(player, checkpoint));
        assert(player.position.y == checkpoint.y);
        assert(player.velocity.x == 0.0f);
        assert(player.air_dash_available);
        assert(player.movement_state == PlayerMovementState::Normal);
    }

    // --- Orbit camera reset and vertical dead zone ---
    {
        CameraController camera_controller;
        CameraState camera;
        camera_controller.Reset(camera, {0.0f, 0.0f, 0.0f});
        const float start_y = camera.origin.y;
        camera_controller.Step(camera, {0.0f, 0.5f, 0.0f}, false, {}, 1.0f / 60.0f);
        assert(camera.origin.y == start_y);
        camera_controller.Step(camera, {0.0f, 2.0f, 0.0f}, false, {}, 1.0f / 60.0f);
        assert(camera.origin.y > start_y);
    }

    // --- Actor default-initialization ---
    {
        Actor a;
        assert(a.position.x == 0.0f);
        assert(a.position.y == 0.0f);
        assert(a.position.z == 0.0f);
        assert(a.active == true);
        assert(a.collected == false);
    }

    // --- SpringActor collectibility ---
    {
        SpringActor s;
        s.Init();
        assert(s.IsCollectible() == true);
    }

    // --- RefillActor uses collected instead of used ---
    {
        RefillActor r;
        r.Init();
        assert(r.collected == false);
        r.OnCollect();
        assert(r.collected == true);
        assert(r.active == false);
    }

    // --- StrawberryActor removes redundant active=false in Update ---
    {
        StrawberryActor s;
        s.Init();
        s.OnCollect();
        assert(s.collected == true);
        assert(s.active == false);
    }

    return 0;
}
