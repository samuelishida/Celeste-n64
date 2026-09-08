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
        assert(player.velocity.x == 0.0f);
        assert(player.velocity.y == 0.0f);
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

    // --- Jump buffering (device-owned 0.10 s buffer) ---
    {
        PlayerState player;
        player.grounded = false;
        player.velocity.y = -1.0f;

        // Inc 1: the input device keeps the pressed edge live for the 0.10 s
        // buffer window (OG VirtualButton parity), so the same PlayerInput
        // snapshot replays the buffered press across ticks until it is
        // consumed on landing.
        PlayerInput jump_input;
        jump_input.jump_pressed = true;
        jump_input.jump_held = true;
        controller.Step(player, jump_input, camera_forward, 1.0f / 60.0f);
        assert(player.velocity.y <= 0.0f);  // airborne, no coyote: buffered, not fired

        player.grounded = true;
        controller.Step(player, jump_input, camera_forward, 1.0f / 60.0f);
        assert(player.velocity.y > 0.0f);   // buffered edge consumed on landing
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

    // --- Wall climb (unified OG-style grab; unlimited grip, no stamina) ---
    {
        PlayerState player;
        player.grounded = false;
        player.velocity.y = -1.0f;
        player.wall_contact = true;
        player.wall_normal = {1.0f, 0.0f, 0.0f};    // wall to the -x side
        // Face into the wall so the facing gate passes.
        player.last_facing = {-1.0f, 0.0f, 0.0f};
        player.target_facing = {-1.0f, 0.0f, 0.0f};

        PlayerInput grab_input;
        grab_input.climb_held = true;
        controller.Step(player, grab_input, camera_forward, 1.0f / 60.0f);
        assert(player.movement_state == PlayerMovementState::Climbing);
        assert(player.velocity.y == 0.0f);  // grab zeroes velocity

        // Stick up climbs at climb_speed.
        PlayerInput climb_up = grab_input;
        climb_up.move = {0.0f, 1.0f};
        controller.Step(player, climb_up, camera_forward, 1.0f / 60.0f);
        assert(player.velocity.y == config.climb_speed);
    }

    // --- Climb release ---
    {
        PlayerState player;
        player.grounded = false;
        player.movement_state = PlayerMovementState::Climbing;
        player.locomotion_state = LocomotionState::Climb;
        player.wall_contact = true;
        player.wall_normal = {1.0f, 0.0f, 0.0f};
        player.last_facing = {-1.0f, 0.0f, 0.0f};
        player.target_facing = {-1.0f, 0.0f, 0.0f};

        PlayerInput release_input;
        release_input.climb_held = false;
        controller.Step(player, release_input, camera_forward, 1.0f / 60.0f);
        assert(player.movement_state == PlayerMovementState::Normal);
        assert(player.velocity.x == 0.0f && player.velocity.z == 0.0f);
    }

    // --- Wall jump from climb: SET velocity away from the wall normal ---
    {
        PlayerState player;
        player.grounded = false;
        player.movement_state = PlayerMovementState::Climbing;
        player.locomotion_state = LocomotionState::Climb;
        player.wall_contact = true;
        player.wall_normal = {1.0f, 0.0f, 0.0f};
        player.last_facing = {-1.0f, 0.0f, 0.0f};
        player.target_facing = {-1.0f, 0.0f, 0.0f};

        PlayerInput wall_jump_input;
        wall_jump_input.climb_held = true;
        wall_jump_input.jump_pressed = true;
        wall_jump_input.jump_held = true;
        controller.Step(player, wall_jump_input, camera_forward, 1.0f / 60.0f);
        assert(player.movement_state == PlayerMovementState::Normal);
        assert(player.velocity.x > 0.0f);   // away from the wall
        assert(player.velocity.y > 0.0f);
    }

    // --- climb-parity-3 Inc 2: Normal-state directional walljump (S3) with
    //     the room == nullptr shim: wall_contact=true + fresh waist-query
    //     shim → SET-away velocity + facing flips away from the wall. ---
    {
        PlayerState player;
        player.grounded = false;
        player.wall_contact = true;
        player.wall_normal = {1.0f, 0.0f, 0.0f};   // wall to the -x side
        player.last_facing = {-1.0f, 0.0f, 0.0f};  // facing into the wall
        player.velocity.y = -20.0f;                // falling

        PlayerInput njump_input;
        njump_input.jump_pressed = true;
        njump_input.jump_held = true;
        controller.Step(player, njump_input, camera_forward, 1.0f / 60.0f);
        assert(player.movement_state == PlayerMovementState::Normal);
        // SET (not add): falling -20 must not bias the launch.
        assert(player.velocity.y == config.wall_jump_speed_y);
        assert(player.velocity.x == config.wall_jump_speed_x);
        assert(player.target_facing.x > 0.0f);     // now facing away
    }

    // --- Climb wall identity: motor's nearest-wall normal flipping between
    //     adjacent faces near an edge/corner must NOT eject the climb. ---
    {
        PlayerState player;
        player.grounded = false;
        player.wall_contact = true;
        player.wall_normal = {1.0f, 0.0f, 0.0f};
        player.last_facing = {-1.0f, 0.0f, 0.0f};
        player.target_facing = {-1.0f, 0.0f, 0.0f};

        PlayerInput grab;
        grab.climb_held = true;
        controller.Step(player, grab, camera_forward, 1.0f / 60.0f);
        assert(player.movement_state == PlayerMovementState::Climbing);

        // Simulate the motor reporting a perpendicular face on later ticks
        // (nearest-wall flip near a corner). Stored grip normal wins.
        grab.move = {0.0f, 1.0f};
        for (int i = 0; i < 5; ++i) {
            player.wall_normal = (i % 2 == 0) ? Vec3{0.0f, 0.0f, -1.0f}
                                              : Vec3{1.0f, 0.0f, 0.0f};
            controller.Step(player, grab, camera_forward, 1.0f / 60.0f);
            assert(player.movement_state == PlayerMovementState::Climbing);
            assert(player.velocity.y == config.climb_speed);  // still going up
        }
    }

    // --- Camera-relative movement ---
    {
        PlayerState player;
        player.grounded = true;

        PlayerInput move_input;
        move_input.move = {0.0f, 1.0f};
        controller.Step(player, move_input, {1.0f, 0.0f, 0.0f}, 1.0f / 60.0f);
        assert(player.velocity.x > 0.0f);
        assert(std::fabs(player.velocity.z) < 1.0f);
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
        // Running fast with reversed input enters Skidding (Inc 4 OG parity).
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
        const Vec3 checkpoint = {0.0f, 20.0f, 0.0f};
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
        camera_controller.Step(camera, {0.0f, 5.0f, 0.0f}, false, false, 0.0f, {}, 1.0f / 60.0f);
        assert(camera.origin.y == start_y);
        camera_controller.Step(camera, {0.0f, 20.0f, 0.0f}, false, false, 0.0f, {}, 1.0f / 60.0f);
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

    // --- Spring launch (Inc 3, OG Player.cs:919-937 parity) ---
    {
        PlayerState player;
        player.grounded = true;
        player.air_dash_available = false;  // spent dash gets refilled
        player.velocity = {20.0f, -5.0f, 20.0f};  // momentum to be killed
        player.coyote_time_remaining = config.coyote_time;
        player.auto_jump = false;
        // Climbing exits to Normal (OG sets stateMachine.State = Normal first).
        player.movement_state = PlayerMovementState::Climbing;
        player.locomotion_state = LocomotionState::Climb;

        const Vec3 spring_pos = {100.0f, 12.0f, 100.0f};
        controller.Spring(player, spring_pos);

        assert(player.movement_state == PlayerMovementState::Normal);
        assert(player.locomotion_state == LocomotionState::Jump);
        // Snaps toward the board: up = +2 toward spring.y + 3 (scalar
        // Approach moves at most 2 per call), XY +4 along the direction toward
        // the spring (distance 141 → moved exactly 4: (2.8284, 2.8284)).
        assert(std::fabs(player.position.y - 2.0f) < 1e-4f);
        assert(std::fabs(player.position.x - (4.0f * 0.70710678f)) < 1e-4f);
        assert(std::fabs(player.position.z - (4.0f * 0.70710678f)) < 1e-4f);
        assert(std::fabs(player.velocity.y - config.spring_jump_speed) < 1e-5f);
        assert(player.hold_jump_speed == config.spring_jump_speed);
        assert(std::fabs(player.hold_jump_time_remaining - config.spring_jump_hold_time) < 1e-5f);
        assert(player.coyote_time_remaining == 0.0f);
        assert(player.auto_jump == true);
        assert(player.air_dash_available == true);
        // Momentum killed: |vx|,|vz| approach 0 at 30 → <= 30 and reduced.
        assert(std::fabs(player.velocity.x) <= 30.0f);
        assert(std::fabs(player.velocity.z) <= 30.0f);
        assert(std::fabs(player.velocity.x) < 20.0f);
        assert(player.contact.ground_snap_cooldown_remaining == config.ground_snap_cooldown);
    }

    // --- SpringActor board cooldown (OG Spring.cs tCooldown = 1.0 s) ---
    {
        SpringActor s;
        s.Init();
        assert(s.cooldown_remaining == 0.0f);
        s.cooldown_remaining = SpringActor::kCooldown;
        s.Update(0.4f);
        assert(s.cooldown_remaining > 0.35f && s.cooldown_remaining < 0.61f);
        s.Update(0.7f);
        assert(s.cooldown_remaining == 0.0f);
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

    // --- Edge collision: player radius overhangs platform edge ---
    // og-mover-overhaul Inc 2: PlayerMotor is deleted; the single mover is
    // MoveService (SweepMove + Popout + ray GroundCheck). This block verifies
    // MoveService compiles/links in the gameplay link set.
    {
        MoveServiceConfig mover_config;
        mover_config.half_height = 5.0f;
        MoveService mover(mover_config);
        (void)mover;
    }

    return 0;
}
