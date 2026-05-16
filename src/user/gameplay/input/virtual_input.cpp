#include "gameplay/input/virtual_input.hpp"

#include <cmath>

namespace madeline_cube {

void UpdateVirtualButton(VirtualButtonState& state, bool down_now, float buffer_seconds, float delta_seconds) {
    const bool was_down = state.down;
    state.down = down_now;
    state.released = was_down && !down_now;
    const bool fresh_press = !was_down && down_now;
    if (fresh_press) {
        state.pressed = true;
        state.consumed = false;
        state.buffer_remaining = buffer_seconds;
    } else if (!state.consumed && state.buffer_remaining > 0.0f) {
        state.buffer_remaining -= delta_seconds;
        if (state.buffer_remaining <= 0.0f) {
            state.buffer_remaining = 0.0f;
            state.pressed = false;
        }
    } else if (state.consumed) {
        state.pressed = false;
        state.buffer_remaining = 0.0f;
    }
}

bool ConsumePress(VirtualButtonState& state) {
    if (!state.pressed || state.consumed) return false;
    state.pressed = false;
    state.consumed = true;
    state.buffer_remaining = 0.0f;
    return true;
}

float ResolveTakeNewerAxis(float negative, float positive, VirtualAxisState& state) {
    const bool neg_down = negative > 0.0f;
    const bool pos_down = positive > 0.0f;
    if (neg_down && !pos_down) state.last_nonzero_sign = -1;
    if (pos_down && !neg_down) state.last_nonzero_sign = 1;
    if (neg_down && pos_down) return state.last_nonzero_sign < 0 ? -negative : positive;
    if (neg_down) return -negative;
    if (pos_down) return positive;
    return 0.0f;
}

VirtualStickState ApplyCircularDeadzone(const Vec2& raw, float deadzone) {
    VirtualStickState out;
    out.raw_value = raw;
    const float len = std::sqrt(raw.x * raw.x + raw.y * raw.y);
    out.value = len < deadzone ? Vec2{} : raw;
    return out;
}

}  // namespace madeline_cube
