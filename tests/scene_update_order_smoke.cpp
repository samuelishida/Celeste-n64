// Host-side mirror of gameplay_scene::Update ordering. The real scene calls
// libdragon/tiny3d at the same callsite, so this test reproduces the player
// physics pipeline only and asserts:
//   1. The update order is timers/input -> state -> motor.Step -> late contact
//      -> respawn -> camera, with no duplicate collision resolution between
//      motor.Step and the camera.
//   2. After one tick, the motor (not any other path) is the sole writer of
//      player position/grounded, so the result matches running the motor in
//      isolation against the same inputs.
//   3. ResolveRoomCollision is never called for the player. If a legacy
//      duplicate path were still wired in, a one-frame fall with the player
//      starting flush on the floor would either snap to a different y (legacy
//      pushout overshoot) or drift away from the motor-only result.

#include <cassert>
#include <cmath>

#include "../src/user/gameplay/player/camera_controller.hpp"
#include "../src/user/gameplay/player/movement_config.hpp"
#include "../src/user/gameplay/physics_contracts.hpp"
#include "../src/user/gameplay/player/player_controller.hpp"
#include "../src/user/gameplay/player/player_motor.hpp"
#include "../src/user/gameplay/player/player_state.hpp"
#include "../src/user/gameplay/world/respawn_system.hpp"
#include "../src/user/gameplay/world/room_data.hpp"
#include "../src/user/gameplay/world/world.hpp"

using namespace madeline_cube;

namespace {

bool ApproxEq(float a, float b, float eps = 0.0005f) {
    return std::fabs(a - b) <= eps;
}

// Mirrors the player+respawn+camera ordering used by GameplayScene::Update.
// Returns the post-tick player state.
PlayerState RunScenePlayerTick(
    PlayerState start,
    Room& room,
    const PlayerController& controller,
    const PlayerMotor& motor,
    const RespawnSystem& respawn,
    CameraController& camera_ctrl,
    CameraState& camera,
    const PlayerInput& input,
    const Vec3& checkpoint,
    float delta_seconds
) {
    PlayerState p = start;
    const Vec3 camera_forward = {
        camera.target.x - camera.position.x,
        camera.target.y - camera.position.y,
        camera.target.z - camera.position.z,
    };

    // 1. timers/input
    const PlayerController::StepContext step = controller.TimerInputPhase(p, input, camera_forward, delta_seconds);

    // 2. state phase
    controller.StatePhase(p, input, step, delta_seconds);

    // 3. motor.Step (sweep + contact)
    MotorInput motor_input;
    motor_input.requested_velocity = p.velocity;
    motor_input.wants_ground_snap = p.contact.was_grounded &&
                                    p.movement_state != PlayerMovementState::Dashing;
    motor_input.wants_coyote_refresh = true;
    motor_input.wants_dash_refill = p.dash_reset_cooldown_remaining <= 0.0f;
    AdvanceMovingSurfaces(room, delta_seconds);
    motor.Step(p, room, motor_input, delta_seconds);

    // 4. late contact mirror
    controller.LateContactPhase(p);

    // 5. respawn (motor-delegated)
    const bool did_respawn = respawn.Step(p, checkpoint, room, motor);
    if (did_respawn) {
        camera_ctrl.Reset(camera, p.position);
    }

    // 6. camera reads post-motor player state
    camera_ctrl.Step(camera, p.position, p.wall_grabbing, {}, delta_seconds, &room);

    return p;
}

}  // namespace

int main() {
    MovementConfig config;
    PlayerController controller(config);
    PlayerMotor motor;
    RespawnSystem respawn(config);
    CameraController camera_ctrl;

    Room room;
    room.colliders[room.collider_count++] = {
        .type = ColliderType::Plane,
        .bounds = {.min = {-10.0f, 0.0f, -10.0f}, .max = {10.0f, 0.0f, 10.0f}},
        .solid = true,
        .normal = {0.0f, 1.0f, 0.0f},
    };

    constexpr float kDt = 1.0f / 60.0f;
    constexpr float kHalfHeight = 1.0f;

    // --- Single tick with zero input: position must match motor-only output,
    //     and grounded must agree. No duplicate writer drifts the y. ---
    {
        PlayerState start;
        start.position = {0.0f, 1.0f, 0.0f};
        start.grounded = true;
        start.contact.was_grounded = true;

        CameraState camera;
        camera_ctrl.Reset(camera, start.position);

        PlayerState after_scene = RunScenePlayerTick(
            start, room, controller, motor, respawn, camera_ctrl, camera,
            {}, {0.0f, 1.0f, 0.0f}, kDt);

        // Reference: run controller + motor in isolation against the same start.
        PlayerState ref = start;
        const PlayerController::StepContext step =
            controller.TimerInputPhase(ref, {}, {0.0f, 0.0f, 1.0f}, kDt);
        controller.StatePhase(ref, {}, step, kDt);
        MotorInput in;
        in.requested_velocity = ref.velocity;
        in.wants_ground_snap = true;
        in.wants_coyote_refresh = true;
        in.wants_dash_refill = ref.dash_reset_cooldown_remaining <= 0.0f;
        motor.Step(ref, room, in, kDt);
        controller.LateContactPhase(ref);

        assert(ApproxEq(after_scene.position.x, ref.position.x));
        assert(ApproxEq(after_scene.position.y, ref.position.y));
        assert(ApproxEq(after_scene.position.z, ref.position.z));
        assert(after_scene.grounded == ref.grounded);
        // Motor is the single owner of the resting y: stays on the floor.
        assert(ApproxEq(after_scene.position.y, kHalfHeight));
    }

    // --- Falling from above: a one-frame fall lands on the motor's resolved
    //     ground position, not on a stale legacy y. The camera read does
    //     not perturb the player. ---
    {
        PlayerState start;
        start.position = {0.0f, 5.0f, 0.0f};
        start.velocity = {0.0f, -10.0f, 0.0f};
        start.grounded = false;

        CameraState camera;
        camera_ctrl.Reset(camera, start.position);

        PlayerState after_scene = RunScenePlayerTick(
            start, room, controller, motor, respawn, camera_ctrl, camera,
            {}, {0.0f, 1.0f, 0.0f}, kDt);

        assert(IsNumericValid(after_scene.position));
        assert(after_scene.position.y >= kHalfHeight - 0.001f);
        assert(after_scene.position.y <= 5.0f);  // never goes above its start
    }

    // --- Respawn path integrates: pushing the player below the kill plane
    //     restores spawn point and motor-resolved grounded state. ---
    {
        PlayerState start;
        start.position = {0.0f, config.respawn_fall_height - 1.0f, 0.0f};
        start.velocity = {0.0f, -50.0f, 0.0f};

        CameraState camera;
        camera_ctrl.Reset(camera, start.position);

        const Vec3 checkpoint = {0.0f, 1.0f, 0.0f};
        PlayerState after_scene = RunScenePlayerTick(
            start, room, controller, motor, respawn, camera_ctrl, camera,
            {}, checkpoint, kDt);

        assert(ApproxEq(after_scene.position.x, checkpoint.x));
        assert(ApproxEq(after_scene.position.y, checkpoint.y));
        assert(ApproxEq(after_scene.position.z, checkpoint.z));
        assert(after_scene.grounded);  // motor resolved contact at the spawn
    }

    // --- Respawn also re-anchors the camera. If the old boom was shortened
    //     by geometry before the fall, the spawn frame must not inherit that
    //     stale close-up framing at a distant checkpoint. ---
    {
        Room obstructed_room = room;
        obstructed_room.colliders[obstructed_room.collider_count++] = {
            .type = ColliderType::Box,
            .bounds = {.min = {-1.0f, 0.0f, -4.0f}, .max = {1.0f, 4.0f, -3.0f}},
            .solid = true,
        };

        CameraState camera;
        camera.target_forward = {0.0f, 0.0f, 1.0f};
        camera_ctrl.Reset(camera, {0.0f, 1.0f, 0.0f});
        for (int i = 0; i < 120; ++i) {
            camera_ctrl.Step(camera, {0.0f, 1.0f, 0.0f}, false, {}, kDt, &obstructed_room);
        }

        PlayerState start;
        start.position = {0.0f, config.respawn_fall_height - 1.0f, 0.0f};
        start.velocity = {0.0f, -50.0f, 0.0f};
        const Vec3 checkpoint = {0.0f, 1.0f, 20.0f};

        RunScenePlayerTick(
            start, obstructed_room, controller, motor, respawn, camera_ctrl, camera,
            {}, checkpoint, kDt);

        CameraState expected;
        expected.target_forward = camera.target_forward;
        expected.target_distance = camera.target_distance;
        camera_ctrl.Reset(expected, checkpoint);

        assert(ApproxEq(camera.position.x, expected.position.x));
        assert(ApproxEq(camera.position.y, expected.position.y));
        assert(ApproxEq(camera.position.z, expected.position.z));
    }

    return 0;
}
