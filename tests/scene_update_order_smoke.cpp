// Host-side mirror of gameplay_scene::Update ordering. The real scene calls
// libdragon/tiny3d at the same callsite, so this test reproduces the player
// physics pipeline only and asserts:
//   1. The update order is timers/input -> state -> MovePhase (single mover)
//      -> respawn -> camera, with no duplicate collision resolution.
//   2. After one tick, MoveService (not any other path) is the sole writer of
//      player position/grounded.
//   3. Legacy duplicate collision paths are not wired in: a one-frame fall
//      with the player starting on the floor stays on the mover-resolved y.
//
// og-mover-overhaul Inc 2: PlayerMotor is deleted; MovePhase owns the mover.

#include <cassert>
#include <cmath>

#include "../src/user/gameplay/player/camera_controller.hpp"
#include "../src/user/gameplay/player/movement_config.hpp"
#include "../src/user/gameplay/physics_contracts.hpp"
#include "../src/user/gameplay/player/player_controller.hpp"
#include "../src/user/gameplay/player/player_movement.hpp"
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
    const MoveService& mover,
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

    // 1. advancing moving surfaces (scene order kept)
    AdvanceMovingSurfaces(room, delta_seconds);

    // 2. timers/input
    PlayerController::StepContext step = controller.TimerInputPhase(p, input, camera_forward, delta_seconds);

    // 3. state phase (velocity edits)
    controller.StatePhase(p, input, step, delta_seconds);

    // 4. MovePhase: single mover (SweepMove + Popout + ray ground) + contact
    controller.MovePhase(p, delta_seconds, &room);

    // 5. respawn (mover-delegated)
    const bool did_respawn = respawn.Step(p, checkpoint, room, mover);
    if (did_respawn) {
        camera_ctrl.Reset(camera, p.position);
    }

    // 6. camera reads post-move player state
    camera_ctrl.Step(camera, p.position, p.movement_state == PlayerMovementState::Climbing, p.grounded, 0.0f, {}, delta_seconds, &room);

    return p;
}

}  // namespace

int main() {
    MovementConfig config;
    PlayerController controller(config);
    MoveService mover;
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
    // Feet-on-floor center: half_height above the floor plane at y=0.
    constexpr float kHalfHeight = 10.0f;

    // --- Single tick with zero input: position must match resolver-only
    //     output, grounded agrees, no duplicate writer drifts the y. ---
    {
        PlayerState start;
        start.position = {0.0f, kHalfHeight, 0.0f};
        start.grounded = true;
        start.contact.was_grounded = true;

        CameraState camera;
        camera_ctrl.Reset(camera, start.position);

        PlayerState after_scene = RunScenePlayerTick(
            start, room, controller, mover, respawn, camera_ctrl, camera,
            {}, {0.0f, kHalfHeight, 0.0f}, kDt);

        // Reference: controller isolated against the same start.
        PlayerState ref = start;
        PlayerController::StepContext step =
            controller.TimerInputPhase(ref, {}, {0.0f, 0.0f, 1.0f}, kDt);
        controller.StatePhase(ref, {}, step, kDt);
        controller.MovePhase(ref, kDt, &room);

        assert(ApproxEq(after_scene.position.x, ref.position.x));
        assert(ApproxEq(after_scene.position.y, ref.position.y));
        assert(ApproxEq(after_scene.position.z, ref.position.z));
        assert(after_scene.grounded == ref.grounded);
        // Mover is the single owner of the resting y: stays on the floor.
        assert(ApproxEq(after_scene.position.y, kHalfHeight, 0.01f));
    }

    // --- Falling from above: one frame of fall stays above the floor and the
    //     camera read does not perturb the player. ---
    {
        PlayerState start;
        start.position = {0.0f, kHalfHeight + 10.0f, 0.0f};
        start.velocity = {0.0f, -10.0f, 0.0f};
        start.grounded = false;
        start.contact.was_grounded = false;

        CameraState camera;
        camera_ctrl.Reset(camera, start.position);

        PlayerState after_scene = RunScenePlayerTick(
            start, room, controller, mover, respawn, camera_ctrl, camera,
            {}, {0.0f, kHalfHeight, 0.0f}, kDt);

        assert(IsNumericValid(after_scene.position));
        assert(after_scene.position.y > kHalfHeight);  // still airborne after 1 frame
        assert(after_scene.position.y <= kHalfHeight + 10.0f);  // never above its start
    }

    // --- Respawn path integrates: pushing below the kill plane restores the
    //     spawn point and mover-resolved grounded state. ---
    {
        PlayerState start;
        start.position = {0.0f, config.respawn_fall_height - 1.0f, 0.0f};
        start.velocity = {0.0f, -50.0f, 0.0f};

        CameraState camera;
        camera_ctrl.Reset(camera, start.position);

        const Vec3 checkpoint = {0.0f, kHalfHeight, 0.0f};
        PlayerState after_scene = RunScenePlayerTick(
            start, room, controller, mover, respawn, camera_ctrl, camera,
            {}, checkpoint, kDt);

        assert(ApproxEq(after_scene.position.x, checkpoint.x));
        assert(ApproxEq(after_scene.position.y, checkpoint.y));
        assert(ApproxEq(after_scene.position.z, checkpoint.z));
        assert(after_scene.grounded);  // mover resolved contact at the spawn
    }

    // --- Respawn also re-anchors the camera. A stale close-up boom at a
    //     distant checkpoint must not be inherited. ---
    {
        Room obstructed_room = room;
        obstructed_room.colliders[obstructed_room.collider_count++] = {
            .type = ColliderType::Box,
            .bounds = {.min = {-1.0f, 0.0f, -4.0f}, .max = {1.0f, 4.0f, -3.0f}},
            .solid = true,
        };

        CameraState camera;
        camera.target_forward = {0.0f, 0.0f, 1.0f};
        camera_ctrl.Reset(camera, {0.0f, kHalfHeight, 0.0f});
        for (int i = 0; i < 120; ++i) {
            camera_ctrl.Step(camera, {0.0f, kHalfHeight, 0.0f}, false, false, 0.0f, {}, kDt, &obstructed_room);
        }

        PlayerState start;
        start.position = {0.0f, config.respawn_fall_height - 1.0f, 0.0f};
        start.velocity = {0.0f, -50.0f, 0.0f};
        const Vec3 checkpoint = {0.0f, kHalfHeight, 20.0f};

        RunScenePlayerTick(
            start, obstructed_room, controller, mover, respawn, camera_ctrl, camera,
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
