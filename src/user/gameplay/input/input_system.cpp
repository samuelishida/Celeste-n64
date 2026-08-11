#include "gameplay/input/input_system.hpp"

#include <libdragon.h>

namespace madeline_cube {

namespace {

// Raw N64 stick range. libdragon reports roughly (-85, +85) on healthy N64
// controllers but up to ±127 on GameCube. Normalize against the documented
// max so a full deflection maps to unit magnitude regardless of controller.
constexpr float kStickMax = 90.0f;

float ClampSigned(float value, float magnitude) {
    if (value > magnitude) return magnitude;
    if (value < -magnitude) return -magnitude;
    return value;
}

void SampleMoveAxis(InputActionVec2& move, const joypad_inputs_t& inputs, float deadzone) {
    // Normalize stick; preserve the project's axis convention used previously:
    //   move_x = -stick_x/max  (stick right -> +X move)
    //   move_y = +stick_y/max  (stick up -> +Y move)
    const float x = -ClampSigned(static_cast<float>(inputs.stick_x), kStickMax) / kStickMax;
    const float y = ClampSigned(static_cast<float>(inputs.stick_y), kStickMax) / kStickMax;

    move.Update(x, y, deadzone);

    // D-Pad contributes digital cardinals to the same Move action (spec §4).
    if (inputs.btn.d_up) move.AddDigital(0.0f, -1.0f);
    if (inputs.btn.d_down) move.AddDigital(0.0f, 1.0f);
    if (inputs.btn.d_left) move.AddDigital(-1.0f, 0.0f);
    if (inputs.btn.d_right) move.AddDigital(1.0f, 0.0f);
}

void SampleCameraAxis(InputActionVec2& camera, const joypad_inputs_t& inputs, float deadzone) {
    // C-Stick is the analog camera input; C-Buttons are its digital fallback
    // (on N64 the cstick is emulated from C-buttons by libdragon anyway).
    const float x = ClampSigned(static_cast<float>(inputs.cstick_x), 90.0f) / 90.0f;
    const float y = ClampSigned(static_cast<float>(inputs.cstick_y), 90.0f) / 90.0f;
    camera.Update(x, y, deadzone);

    // C-Buttons add digital camera input (orbit = X, zoom = Y).
    if (inputs.btn.c_left) camera.AddDigital(-1.0f, 0.0f);
    if (inputs.btn.c_right) camera.AddDigital(1.0f, 0.0f);
    if (inputs.btn.c_up) camera.AddDigital(0.0f, -1.0f);
    if (inputs.btn.c_down) camera.AddDigital(0.0f, 1.0f);
}

}  // namespace

void InputSystem::Poll() {
    // libdragon requires joypad_poll() each frame (gameplay_scene already does
    // this before calling Poll; keep a defensive call here is NOT added so we
    // avoid double-polling. Callers MUST joypad_poll() before Poll()).

    const joypad_inputs_t inputs = joypad_get_inputs(JOYPAD_PORT_1);
    const joypad_buttons_t held = joypad_get_buttons_held(JOYPAD_PORT_1);

    SampleMoveAxis(move, inputs, deadzone);
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
