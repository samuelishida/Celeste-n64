#pragma once

#include <cstdint>

#include "gameplay/math_types.hpp"
#include "gameplay/input/virtual_input.hpp"

namespace madeline_cube {

// Input snapshot consumed by the PlayerController each physics tick.
//
// Pressed edges are CONSUMABLE (spec §1, §42): gameplay actions call the
// Consume*() helpers after claiming an edge so a single physical press can
// never fire more than one action (e.g. wall-jump + normal jump + dash-jump
// in the same frame). The flags are `mutable` so a const PlayerInput& can be
// consumed by the controller's const StatePhase.
// controls-physics-port Inc 1: the input DEVICE owns the 0.10 s press buffer
// (OG VirtualButton parity) — `jump_pressed`/`dash_pressed` are the
// device-buffered edges, live for the buffer window until consumed. The
// controller no longer owns buffer timers.
struct PlayerInput {
    Vec2 move;
    bool jump_pressed = false;
    bool jump_held = false;
    bool dash_pressed = false;
    bool climb_held = false;

    // True once the jump press has been claimed by an action this frame.
    mutable bool jump_press_consumed = false;
    // True once the dash press has been claimed by an action this frame.
    mutable bool dash_press_consumed = false;

    // Device-level consume propagation (Inc 1): consuming the PlayerInput
    // copy must ALSO consume the device edge — otherwise the buffered press
    // re-fires on the next frame's snapshot (OG VirtualButton.ConsumePress
    // parity). Set by ReadPlayerInput; null in host fixtures that build
    // PlayerInput by hand.
    VirtualButtonState* jump_device = nullptr;
    VirtualButtonState* dash_device = nullptr;

    // Claim the jump press edge. Returns true if a press was available and
    // not already claimed; afterwards jump_pressed() reads false.
    bool ConsumeJumpPress() const {
        if (jump_pressed && !jump_press_consumed) {
            jump_press_consumed = true;
            if (jump_device != nullptr) ConsumePress(*jump_device);
            return true;
        }
        return false;
    }
    // Claim the dash press edge. Returns true if a press was available and
    // not already claimed; afterwards dash_pressed() reads false.
    bool ConsumeDashPress() const {
        if (dash_pressed && !dash_press_consumed) {
            dash_press_consumed = true;
            if (dash_device != nullptr) ConsumePress(*dash_device);
            return true;
        }
        return false;
    }
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
    int16_t wall_surface_id = -1;
    // Stable identity of the gripped static-collmesh triangle (array index),
    // for persistent re-acquire. `wall_surface_id` is the moving-surface owner
    // (=-1 for static), NOT a per-triangle identity, so grip persistence keys
    // on this. -1 = no gripped face.
    int32_t grip_face_id = -1;
    float cooldown_remaining = 0.0f;
    // Grip-grace timer: while climbing, a brief probe miss (geometry seams,
    // corners, floating-point at the tangent boundary) must not drop the grab
    // immediately. Counts down from kClimbGripGrace; the grab is released only
    // if grip stays absent for the whole window. Reset to full on every tick
    // where grip is present.
    float grip_lost_timer = 0.0f;
    // Consecutive ticks the ledge/mantle probe has reported no wall. The
    // ledge probe uses the tight radius and can flicker at the tangency
    // boundary mid-wall; the mantle must not fire on a single-frame flicker,
    // only after the ledge is confirmed clear for several ticks (real lip).
    int ledge_miss_ticks = 0;

    // ── OG corner system (climb-parity-3 Inc 3; OG climbCorner* fields) ──
    // Corner ease: on an outer-corner adopt, position + facing lerp from
    // corner_from → corner_to over climb_corner_ease_time (0.2 s). 0 = idle;
    // while > 0 the ease branch REPLACES normal wall movement (OG structure).
    Vec3 corner_from{};
    Vec3 corner_to{};
    Vec3 corner_facing_from{};
    Vec3 corner_facing_to{};
    // Travel origin for OG's "> 2 units traveled → force a corner ease"
    // trigger: stashed on every corner adopt / ease end, so displacement
    // accumulates across ticks exactly like OG's (Position - climbCornerFrom)
    // check (climbCornerFrom only resets when a corner eases).
    Vec3 corner_travel_origin{};
    float corner_ease = 0.0f;
};

struct PlayerState {
    Vec3 position = {};
    Vec3 prev_position = {};
    Vec3 velocity = {};
    Vec3 facing = {0.0f, 0.0f, 1.0f};
    Vec3 target_facing = {0.0f, 0.0f, 1.0f};
    Vec3 last_facing = {0.0f, 0.0f, 1.0f};
    bool grounded = false;
    bool air_dash_available = true;
    float dash_time_remaining = 0.0f;
    float dash_cooldown_remaining = 0.0f;
    float dash_reset_cooldown_remaining = 0.0f;
    float no_dash_jump_remaining = 0.0f;
    bool dashed_on_ground = false;
    PlayerMovementState movement_state = PlayerMovementState::Normal;
    LocomotionState locomotion_state = LocomotionState::Idle;
    float dash_hitstop_remaining = 0.0f;
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
