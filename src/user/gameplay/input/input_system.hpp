#pragma once

#include <cstdint>

// libdragon joypad types are only needed for the joypad polling implementation
// (input_system.cpp), not for the pure logic classes. Guard the include so the
// header can be compiled on the host (smoke tests) without the N64 toolchain.
#ifdef __N64__
#include <libdragon.h>
#endif

#include "gameplay/math_types.hpp"
#include "gameplay/input/input_button.hpp"

namespace madeline_cube {

namespace input_detail {
// Clamp `value` to [-magnitude, +magnitude].
inline float ClampSigned(float value, float magnitude) {
    if (value > magnitude) return magnitude;
    if (value < -magnitude) return -magnitude;
    return value;
}
}  // namespace input_detail

// Pure, host-compilable analog stick normalization. Returns RAW per-axis
// coords only — NOT a resolved movement vector and NOT deadzoned/clamped to
// unit magnitude. Both of those happen downstream in InputActionVec2::Update
// (which clamps a full diagonal to magnitude 1.0). Kept in the header so the
// host smoke tests can exercise the real move-scaling logic without the N64
// libdragon include.
//
// Convention (matches pre-overhaul ReadPlayerInput): stick right -> +X,
// stick up -> +Y, result = { -clamp(x,max)/max, clamp(y,max)/max }.
//
// D-pad is intentionally NOT sampled here: the pre-overhaul feel had only the
// analog stick drive Move, so the D-pad cardinals were removed from the move
// path. Do not re-add them.
inline Vec2 SampleStickAxis(int16_t stick_x, int16_t stick_y, float stick_max) {
    return {
        -input_detail::ClampSigned(static_cast<float>(stick_x), stick_max) / stick_max,
         input_detail::ClampSigned(static_cast<float>(stick_y), stick_max) / stick_max,
    };
}

// InputSystem is the single entry point for all controller input in the ROM.
//
// It adapts the Celeste64 controls spec (§1, §41) to the physical N64
// controller via libdragon's unified joypad API:
//
//   Move   -> left analog stick
//   Camera -> C-Stick (analog) + C-Buttons   (digital orbit/zoom)
//   Jump   -> A  (also Y on GameCube via joypad)
//   Dash   -> B  (also X on GameCube via joypad)
//   Climb  -> Z / L / R
//   Pause  -> Start
//
// The spec's Xbox/keyboard default bindings map onto the N64 controller as
// follows (there is no keyboard on real N64 hardware):
//   A/Y  (Jump)  -> A (N64 face)
//   X/B  (Dash)  -> B (N64 face)
//   LB/RB/LT/RT  -> L / R / Z  (shoulders + trigger)
//   Start (Pause)-> Start
//
// Each call to Poll() samples the joypad once, computes pressed/released
// transitions, and stores the analog actions. Gameplay reads the results via
// the exposed actions. Button presses are CONSUMABLE (see InputButton) so a
// single physical press can be claimed by exactly one gameplay action.
class InputSystem {
public:
    // Sample the controller for the current frame. Must be called once per
    // frame before reading any action/button state.
    void Poll();

    // Bindings are exposed as public actions for direct gameplay reads.
    InputActionVec2 move;
    InputActionVec2 camera;

    InputButton jump;
    InputButton dash;
    InputButton climb;
    InputButton pause;

    // True if the left stick produced a non-zero move this frame.
    bool HasMove() const { return move.Value().x != 0.0f || move.Value().y != 0.0f; }

    // Convenience: translate the Move action into the PlayerInput struct the
    // player controller already consumes.
    Vec2 MoveValue() const { return move.Value(); }

    // Deadzone applied to the c-stick for camera input. Pre-overhaul had no
    // deadzone on camera (pure digital C-buttons). Kept at 0 to match.
    // The move axis ignores this (it passes 0.0f explicitly in SampleMoveAxis).
    float deadzone = 0.0f;
};

}  // namespace madeline_cube
