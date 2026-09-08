#pragma once

#include <cmath>

#include "gameplay/math_types.hpp"

namespace madeline_cube {

// VirtualButtonState models a pressable action with the OG Celeste64
// VirtualButton semantics (Controls.cs): a physical press arms a `buffer`
// window during which `pressed` stays live even after the button is released
// (jump-before-landing), and the edge is consume-once so a single press can
// never fire two actions.
struct VirtualButtonState {
    bool down = false;
    bool pressed = false;
    bool released = false;
    bool consumed = false;
    float buffer_remaining = 0.0f;
};

// Feed the raw held state for this frame. `buffer_seconds` is the press-buffer
// window (0.10 s for jump/dash, 0 for unbuffered buttons like climb/pause).
// Header-inline (Pattern A): the N64 build and the host smokes both consume
// this layer without linking a .cpp.
inline void UpdateVirtualButton(VirtualButtonState& state, bool down_now, float buffer_seconds, float delta_seconds) {
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
    } else if (state.consumed || state.buffer_remaining <= 0.0f) {
        // Consumed, or unbuffered (buffer_seconds == 0): the pressed edge is a
        // single-frame event — clear it on the next update. Without this branch
        // an unbuffered button (climb/pause) would keep `pressed` sticky until
        // consumed, re-firing every frame.
        state.pressed = false;
        state.buffer_remaining = 0.0f;
    }
}

// Claim the press edge (OG VirtualButton.ConsumePress). Returns true if a
// press was available and not already claimed; afterwards `pressed` reads
// false and the edge cannot re-fire for the rest of the buffer window.
inline bool ConsumePress(VirtualButtonState& state) {
    if (!state.pressed || state.consumed) return false;
    state.pressed = false;
    state.consumed = true;
    state.buffer_remaining = 0.0f;
    return true;
}

struct VirtualAxisState {
    float value = 0.0f;
    int last_nonzero_sign = 0;
};

// OG VirtualStick.TakeNewer: when both the negative and positive halves of an
// axis are down, keep the last non-zero direction (menu-style input).
inline float ResolveTakeNewerAxis(float negative, float positive, VirtualAxisState& state) {
    const bool neg_down = negative > 0.0f;
    const bool pos_down = positive > 0.0f;
    if (neg_down && !pos_down) state.last_nonzero_sign = -1;
    if (pos_down && !neg_down) state.last_nonzero_sign = 1;
    if (neg_down && pos_down) return state.last_nonzero_sign < 0 ? -negative : positive;
    if (neg_down) return -negative;
    if (pos_down) return positive;
    return 0.0f;
}

struct VirtualStickState {
    Vec2 raw_value;
    Vec2 value;
};

// OG VirtualStick deadzone: a stick deflection inside the circular deadzone
// radius is zeroed; outside it the raw value passes through unchanged.
inline VirtualStickState ApplyCircularDeadzone(const Vec2& raw, float deadzone) {
    VirtualStickState out;
    out.raw_value = raw;
    const float len = std::sqrt(raw.x * raw.x + raw.y * raw.y);
    out.value = len < deadzone ? Vec2{} : raw;
    return out;
}

}  // namespace madeline_cube
