#include "gameplay/input/input_system.hpp"

#include <libdragon.h>

namespace madeline_cube {

namespace {

// Raw N64 stick range. libdragon reports roughly (-85, +85) on healthy N64
// controllers but up to ±127 on GameCube. Normalize against a fixed max so a
// full deflection maps to unit magnitude.
//
// NOTE: this is intentionally 80 (matching the pre-overhaul feel), while the
// camera axis below deliberately stays at 90. Do not unify them — move feel is
// tuned to 80, camera feel is out of scope.
constexpr float kStickMax = 80.0f;

float ClampSigned(float value, float magnitude) {
    if (value > magnitude) return magnitude;
    if (value < -magnitude) return -magnitude;
    return value;
}

void SampleMoveAxis(InputActionVec2& move, const joypad_inputs_t& inputs) {
    // Normalize stick via the pure host-testable helper (see input_system.hpp).
    //   move_x = -stick_x/max  (stick right -> +X move)
    //   move_y = +stick_y/max  (stick up -> +Y move)
    const Vec2 s = SampleStickAxis(inputs.stick_x, inputs.stick_y, kStickMax);
    // No deadzone on the move axis (matches pre-overhaul). The unit-magnitude
    // clamp for a full diagonal happens inside Update.
    move.Update(s.x, s.y, 0.0f);
}

void SampleCameraAxis(InputActionVec2& camera, const joypad_inputs_t& inputs, float deadzone) {
    // C-Stick is the analog camera input; C-Buttons are its digital fallback.
    // On N64 the cstick is emulated from C-buttons by libdragon, so both fire
    // simultaneously — the digital contributions MUST have the same sign as the
    // analog axis, otherwise they cancel out and the camera does nothing.
    // The 90.0f here deliberately differs from the move axis' kStickMax=80
    // (camera feel is out of scope) — do not unify.
    const float x = ClampSigned(static_cast<float>(inputs.cstick_x), 90.0f) / 90.0f;
    const float y = ClampSigned(static_cast<float>(inputs.cstick_y), 90.0f) / 90.0f;
    camera.Update(x, y, deadzone);

    // C-Buttons reinforce the analog axis (same sign). On N64 both paths fire
    // together, so AddDigital reinforces rather than cancels. On GameCube (true
    // analog cstick), the C-buttons provide a digital fallback.
    //   orbit: C-right = +X (rotate right), C-left = -X (rotate left)
    //   zoom:  C-up = +Y (zoom out / distance+), C-down = -Y (zoom in / distance-)
    // This matches the pre-overhaul ReadCameraInput() sign convention.
    if (inputs.btn.c_left) camera.AddDigital(-1.0f, 0.0f);
    if (inputs.btn.c_right) camera.AddDigital(1.0f, 0.0f);
    if (inputs.btn.c_up) camera.AddDigital(0.0f, 1.0f);
    if (inputs.btn.c_down) camera.AddDigital(0.0f, -1.0f);
}

}  // namespace

void InputSystem::Poll() {
    // libdragon requires joypad_poll() each frame (gameplay_scene already does
    // this before calling Poll; keep a defensive call here is NOT added so we
    // avoid double-polling. Callers MUST joypad_poll() before Poll()).

    const joypad_inputs_t inputs = joypad_get_inputs(JOYPAD_PORT_1);
    const joypad_buttons_t held = joypad_get_buttons_held(JOYPAD_PORT_1);

    SampleMoveAxis(move, inputs);
    SampleCameraAxis(camera, inputs, deadzone);

    // Face buttons: Jump = A (Y is a GameCube-only face button, treated as an
    // alias of the same action per spec §3).
    const bool jump_held = (held.a != 0) || (held.y != 0);
    const bool dash_held = (held.b != 0) || (held.x != 0);
    const bool climb_held = (held.z != 0) || (held.l != 0) || (held.r != 0);
    const bool pause_held = (held.start != 0);

    // Analog L/R triggers also count as Climb (spec §3: LT/RT -> Climb).
    const bool trigger_climb = (inputs.analog_l > 80) || (inputs.analog_r > 80);
    const bool climb_total = climb_held || trigger_climb;

    jump.Update(jump_held);
    dash.Update(dash_held);
    climb.Update(climb_total);
    pause.Update(pause_held);
}

}  // namespace madeline_cube
