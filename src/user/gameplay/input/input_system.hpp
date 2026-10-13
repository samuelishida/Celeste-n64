#pragma once

#include <cstdint>

// libdragon joypad types are only needed for the joypad polling implementation
// (input_system.cpp), not for the pure logic classes. Guard the include so the
// header can be compiled on the host (smoke tests) without the N64 toolchain.
#ifdef N64
#include <libdragon.h>
#endif

#include "gameplay/math_types.hpp"
#include "gameplay/input/input_button.hpp"
#include "gameplay/input/virtual_input.hpp"

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
// unit magnitude. Both of those happen downstream (the circular deadzone in
// Poll, and the unit-magnitude clamp in the controller's RelativeMoveInput).
// Kept in the header so the host smoke tests can exercise the real move-scaling
// logic without the N64 libdragon include.
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
//   Climb  -> SHOULDERS ONLY: Z / L / R digital. The GCN analog trigger
//             fields (joypad_inputs_t.analog_l/analog_r) are deliberately NOT
//             read — spurious non-zero values there present as constant
//             grabbing with no button held.
//   Pause  -> Start
//
// The spec's Xbox/keyboard default bindings map onto the N64 controller as
// follows (there is no keyboard on real N64 hardware):
//   A/Y  (Jump)  -> A (N64 face)
//   X/B  (Dash)  -> B (N64 face)
//   LB/RB/LT/RT  -> L / R / Z  (shoulders + trigger)
//   Start (Pause)-> Start
//
// Poll once per rendered frame; observed edges survive zero-tick frames.
// BeginTick advances jump/dash buffers in simulation time and delivers each
// raw edge once. Climb is an immediate held action, with no debounce/buffer.
class InputSystem {
public:
    // Sample the controller for the current frame. Must be called once per
    // frame. delta_seconds only drives the render-frame Pause action;
    // simulation buffers advance through BeginTick.
    void Poll(float delta_seconds = 1.0f / 60.0f);
    // Pure device boundary, shared by joypad sampling and host fixtures.
    // Disconnection supplies false for every held bit, preserving releases.
    void ObserveGameplayButtons(bool jump, bool dash, bool climb) {
        polled_jump_.Poll(jump);
        polled_dash_.Poll(dash);
        polled_climb_.Poll(climb);
    }
    void BeginTick(float dt) {
        polled_jump_.BeginTick(jump_, 0.10f, dt);
        polled_dash_.BeginTick(dash_, 0.10f, dt);
        polled_climb_.BeginTick(climb_, 0.0f, dt);
    }
    void EndTick(bool jump_claimed, bool dash_claimed) {
        if (jump_claimed) ConsumeJumpPress();
        if (dash_claimed) ConsumeDashPress();
    }

    // Tick states: read only after BeginTick, then commit accepted claims.
    const VirtualStickState& Move() const { return move_; }
    const VirtualButtonState& Jump() const { return jump_; }
    const VirtualButtonState& Dash() const { return dash_; }
    const VirtualButtonState& Climb() const { return climb_; }
    const VirtualButtonState& Pause() const { return pause_; }

    // Camera stays an InputActionVec2 (C-button digital reinforcement); its
    // consumer ReadCameraInput reads camera.Value().
    InputActionVec2 camera;

    // OG ControlsConfig::Move uses a 0.35 circular deadzone. Keep configurable
    // for controller A/B, but default to OG so idle stick noise stays neutral.
    float move_deadzone = 0.35f;

    // Deadzone applied to the c-stick for camera input. Pre-overhaul had no
    // deadzone on camera (pure digital C-buttons). Kept at 0 to match.
    float deadzone = 0.0f;

    // True if the left stick produced a non-zero move this frame.
    bool HasMove() const { return move_.value.x != 0.0f || move_.value.y != 0.0f; }

    // Convenience: the resolved Move value (post-deadzone).
    Vec2 MoveValue() const { return move_.value; }

    // Device-level consume (Inc 1): the scene consumes the buffered dash edge
    // on respawn so a pre-death press cannot fire after respawn (the old
    // ResetState cleared the controller's dash_buffer_remaining).
    void ConsumeJumpPress() { ConsumePress(jump_); }
    void ConsumeDashPress() { ConsumePress(dash_); }

    // Raw controller snapshot from the most recent Poll() — diagnostics only
    // (telemetry prints it so a stuck button bit is visible in Ares captures).
    struct RawSnapshot {
        uint8_t style = 0;
        uint16_t buttons_current = 0;
        uint16_t buttons_held = 0;
        uint8_t z = 0;
        uint8_t l = 0;
        uint8_t r = 0;
        int16_t stick_x = 0;
        int16_t stick_y = 0;
        int16_t analog_l = 0;
        int16_t analog_r = 0;
        bool jump_down = false;
        bool dash_down = false;
        bool climb_down = false;
        bool pause_down = false;
    };
    const RawSnapshot& LastRaw() const { return last_raw_; }

private:
    VirtualStickState move_;
    VirtualButtonState jump_;
    VirtualButtonState dash_;
    VirtualButtonState climb_;
    PolledButtonState polled_jump_, polled_dash_, polled_climb_;
    VirtualButtonState pause_;
    RawSnapshot last_raw_;
};

}  // namespace madeline_cube
