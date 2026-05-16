#pragma once

#include "gameplay/math_types.hpp"

namespace madeline_cube {

struct VirtualButtonState {
    bool down = false;
    bool pressed = false;
    bool released = false;
    bool consumed = false;
    float buffer_remaining = 0.0f;
};

void UpdateVirtualButton(VirtualButtonState& state, bool down_now, float buffer_seconds, float delta_seconds);
bool ConsumePress(VirtualButtonState& state);

struct VirtualAxisState {
    float value = 0.0f;
    int last_nonzero_sign = 0;
};

float ResolveTakeNewerAxis(float negative, float positive, VirtualAxisState& state);

struct VirtualStickState {
    Vec2 raw_value;
    Vec2 value;
};

VirtualStickState ApplyCircularDeadzone(const Vec2& raw, float deadzone);

}  // namespace madeline_cube
