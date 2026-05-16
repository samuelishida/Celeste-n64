#include <cassert>

#include "../src/user/gameplay/player/player_controller.hpp"
#include "../src/user/gameplay/player/player_motor.hpp"

using namespace madeline_cube;

int main() {
    MovementConfig config;
    PlayerController controller(config);
    const Vec3 camera_forward = {0.0f, 0.0f, 1.0f};

    // Source-required runtime bookkeeping exists before the new motor owns it.
    {
        PlayerState player;
        assert(player.contact.ground_normal.y == 1.0f);
        assert(player.contact.coyote_height == 0.0f);
        assert(player.contact.ground_snap_cooldown_remaining == 0.0f);
        assert(player.platform_carry.time_remaining == 0.0f);
        assert(player.climb.wall_surface_id == -1);
        assert(player.dash_count == 1);
    }

    // Timer/input captures pre-state velocity and only updates timers/input facts.
    {
        PlayerState player;
        player.velocity = {3.0f, -2.0f, 1.0f};
        player.no_move_time_remaining = 1.0f;
        player.contact.ground_snap_cooldown_remaining = 1.0f;
        player.platform_carry.time_remaining = 1.0f;

        PlayerInput input;
        input.move = {1.0f, 0.0f};
        const PlayerController::StepContext context =
            controller.TimerInputPhase(player, input, camera_forward, 0.25f);

        assert(context.has_move_input);
        assert(player.position.x == 0.0f);
        assert(player.velocity.x == 3.0f);
        assert(player.contact.previous_velocity.x == 3.0f);
        assert(player.contact.previous_velocity.y == -2.0f);
        assert(player.no_move_time_remaining == 0.75f);
        assert(player.contact.ground_snap_cooldown_remaining == 0.75f);
        assert(player.platform_carry.time_remaining == 0.75f);
    }

    // State changes velocity, motor applies position, and late contact observes resolver results.
    {
        Room room;
        room.colliders[room.collider_count++] = {
            .type = ColliderType::Plane,
            .bounds = {.min = {-10.0f, 0.0f, -10.0f}, .max = {10.0f, 0.0f, 10.0f}},
            .solid = true,
        };

        PlayerMotor motor;

        PlayerState player;
        player.position = {0.0f, 1.0f, 0.0f};
        player.grounded = true;

        PlayerInput input;
        input.jump_pressed = true;
        input.jump_held = true;
        const PlayerController::StepContext context =
            controller.TimerInputPhase(player, input, camera_forward, 1.0f / 60.0f);
        controller.StatePhase(player, input, context, 1.0f / 60.0f);
        assert(player.velocity.y > 0.0f);
        assert(player.position.y == 1.0f);  // controller does NOT move the player

        MotorInput motor_input;
        motor_input.requested_velocity = player.velocity;
        motor_input.wants_ground_snap = false;  // takeoff frame
        motor.Step(player, room, motor_input, 1.0f / 60.0f);
        assert(player.position.y > 1.0f);

        controller.LateContactPhase(player);
        // Late contact mirrors current grounded into was_grounded for next frame.
    }

    return 0;
}
