#pragma once

#include <cstdint>

#include "gameplay/math_types.hpp"
#include "gameplay/player/player_animation.hpp"
#include "gameplay/input/virtual_input.hpp"

namespace madeline_cube {

// Values are immutable during a simulation tick. Mutable claim receipts are
// local to that snapshot, so two launch helpers cannot claim the same press.
// The scene commits receipts after the handler; there are no device pointers.
struct PlayerInput {
    Vec2 move;
    bool jump_pressed = false; // buffered action availability
    bool jump_edge = false; // observed press, first tick only
    bool jump_held = false;
    bool jump_released = false;
    bool dash_pressed = false;
    bool dash_edge = false;
    bool climb_held = false;
    bool dash_released = false;
    bool climb_pressed = false;
    bool climb_released = false;
    // Raw controller bits copied once by ReadPlayerInput for diagnostics only.
    // -1 means a host fixture supplied this snapshot without a device sample.
    int8_t raw_climb_z = -1;
    int8_t raw_climb_l = -1;
    int8_t raw_climb_r = -1;

    // True once the jump press has been claimed by an action this frame.
    mutable bool jump_press_consumed = false;
    // True once the dash press has been claimed by an action this frame.
    mutable bool dash_press_consumed = false;

    bool JumpPressAvailable() const { return jump_pressed && !jump_press_consumed; }
    bool DashPressAvailable() const { return dash_pressed && !dash_press_consumed; }

    // Claim the jump press edge. Returns true if a press was available and
    // not already claimed; afterwards jump_pressed() reads false.
    bool ConsumeJumpPress() const {
        if (!JumpPressAvailable()) return false;
        jump_press_consumed = true;
        return true;
    }
    // Claim the dash press edge. Returns true if a press was available and
    // not already claimed; afterwards dash_pressed() reads false.
    bool ConsumeDashPress() const {
        if (!DashPressAvailable()) return false;
        dash_press_consumed = true;
        return true;
    }
};

// State handlers request displacement without borrowing velocity. A climb
// continuation runs after this request resolves, before the velocity sweep.
struct PlayerMoveIntent {
    bool has_climb_delta = false;
    Vec3 climb_delta{};
};

enum class PlayerMovementState : uint8_t {
    Normal = 0,
    Dashing,
    Climbing,
    Skidding,  // og-mover-overhaul Inc 4 (OG States.Skidding)
};

enum class LocomotionState : uint8_t {
    Idle = 0,
    Run,
    Jump,
    Dash,
    Climb,
    Fall,
};

struct ContactState {
    Vec3 ground_normal = {0.0f, 1.0f, 0.0f};
    float coyote_height = 0.0f;
    int ground_surface_id = -1;
    Vec3 previous_velocity;
    float ground_snap_cooldown_remaining = 0.0f;
    bool was_grounded = false;
};

struct PlatformCarryState {
    Vec3 stored_velocity;
    float time_remaining = 0.0f;
};

struct ClimbState {
    Vec3 wall_normal;
    int32_t wall_surface_id = -1;
    // Stable identity of the gripped static-collmesh triangle (array index),
    // for persistent re-acquire. `wall_surface_id` is the moving-surface owner
    // (=-1 for static), NOT a per-triangle identity, so grip persistence keys
    // on this. -1 = no gripped face.
    int32_t grip_face_id = -1;
    float cooldown_remaining = 0.0f;
    int input_sign = 1; // OG climbInputSign, latched until raw lateral recenters.

    // ── OG corner system (climb-parity-3 Inc 3; OG climbCorner* fields) ──
    // Corner ease: on an outer-corner adopt, position + facing lerp from
    // corner_from → corner_to over climb_corner_ease_time (0.2 s). 0 = idle;
    // while > 0 the ease branch REPLACES normal wall movement (OG structure).
    Vec3 corner_from{};
    Vec3 corner_to{};
    Vec3 corner_normal_from{};
    Vec3 corner_normal_to{};
    Vec3 corner_facing_from{};
    Vec3 corner_facing_to{};
    Vec3 corner_camera_from{}, corner_camera_to{}, camera_forward{};
    bool corner_camera_active = false;
    bool camera_forward_changed = false;
    float corner_ease = 0.0f;
};

// Timer ownership (setters / decay):
// jump/dash press buffers: InputSystem observed polls / BeginTick.
// dash cooldown: OnStateExit(Dashing) / TimerInputPhase.
// dash reset: TryDashAccept and climb dash / TimerInputPhase.
// no-dash-jump: dash entry / StDashingUpdate; no-skid-jump: skid entry /
//               StSkiddingUpdate.
// coyote: TimerInputPhase decrements before dispatch; LateContact refreshes on
//         support; jump helpers clear it.
// hold jump: jump/dash-jump/wall-jump/skid-jump/spring setters / TimerInputPhase
//            decrements before dispatch; normal gravity clears on release.
// no move: mantle / TimerInputPhase.
// no skid jump: Normal -> Skidding entry / TimerInputPhase.
// climb cooldown: mantle and climb release/launch / TimerInputPhase.
// ground snap cancel: all takeoffs + sustained climb / TimerInputPhase.
// platform carry: MovingSurfacePhase / TimerInputPhase; launch consumes once.
// world hitstop: requests commit at tick end; GameplayScene consumes whole
//                world ticks before input buffers, timers, camera, actors,
//                hair, or player movement.
// dash active: dash entry / StDashingUpdate after hitstop.
// corner ease and grip loss: StClimbingUpdate only. These policies are reviewed
// in the climb parity increment, without transferring ownership to the scene.
struct PlayerState {
    PlayerAnimation animation;
    Vec3 model_scale = {1.0f, 1.0f, 1.0f};
    uint32_t movement_contact_reset_count = 0;
    Vec3 position = {};
    Vec3 prev_position = {};
    Vec3 velocity = {};
    Vec3 facing = {0.0f, 0.0f, 1.0f};
    Vec3 target_facing = {0.0f, 0.0f, 1.0f};
    Vec3 last_facing = {0.0f, 0.0f, 1.0f};
    bool grounded = false;
    // OG Player owns an integer charge count, not a boolean. Double refills
    // can grant two; Spring/DashJump and grounded refill only raise to one.
    int dashes = 1;
    // OG RefillDash flashes hair white for .05 s; presentation consumes this
    // simulation-ticked timer, never wall-clock render time.
    float dash_refill_flash_remaining = 0.0f;
    float dash_time_remaining = 0.0f;
    float dash_cooldown_remaining = 0.0f;
    float dash_reset_cooldown_remaining = 0.0f;
    float no_dash_jump_remaining = 0.0f;
    bool dashed_on_ground = false;
    PlayerMovementState movement_state = PlayerMovementState::Normal;
    LocomotionState locomotion_state = LocomotionState::Idle;
    float world_hitstop_remaining = 0.0f;
    float world_hitstop_request = 0.0f;
    float dash_active_remaining = 0.0f;

    // Coyote time: counts down after leaving ground.
    float coyote_time_remaining = 0.0f;

    // Celeste-like jump sustain.
    float hold_jump_time_remaining = 0.0f;
    float hold_jump_speed = 0.0f;
    bool auto_jump = false;

    // Movement timer.
    float no_move_time_remaining = 0.0f;

    // Skid state lives in `movement_state == Skidding` (enum is authoritative);
    // the old redundant `skidding` bool was removed in review (desynced from
    // the FSM when a dash was accepted mid-skid).
    // tNoSkidJump (OG StSkiddingEnter): skid-jump unlock delay after entering
    // skid (0.10 s). Counts down in the timer block.
    float no_skid_jump_remaining = 0.0f;

    // Surface material flags (set by the mover alongside `grounded`).
    bool on_ice = false;
    bool on_oneway = false;
    // Set by MoveService::Popout when the repaired contact face carries
    // MAT_DEATH (spikes/lava). Consumed by the scene's death evaluation — the
    // old PlayerMotor owned this check; the single mover owns it now.
    bool death_contact = false;

    ContactState contact;
    PlatformCarryState platform_carry;
    ClimbState climb;

    // Collision probes (set by caller before Step)
    bool wall_contact = false;
    // Last MoveService tick resolved against an overhead solid. Retained for
    // physics telemetry and cleared by the next non-climb Popout pass.
    bool ceiling_contact = false;
    // Wall normal from the most recent wall contact (set by motor or test fixture).
    Vec3 wall_normal = {0.0f, 0.0f, 0.0f};

    Vec3 InterpolatedPosition(float alpha) const {
        return {
            prev_position.x + (position.x - prev_position.x) * alpha,
            prev_position.y + (position.y - prev_position.y) * alpha,
            prev_position.z + (position.z - prev_position.z) * alpha,
        };
    }
};

}  // namespace madeline_cube
