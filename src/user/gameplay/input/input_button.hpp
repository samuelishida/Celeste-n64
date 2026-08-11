#pragma once

#include <cmath>
#include <cstdint>

#include "gameplay/math_types.hpp"

namespace madeline_cube {

// InputButton models a pressable action with the exact state semantics the
// Celeste64 Player expects (§1 of the controls spec):
//
//   Down     -> the button is held this frame
//   Pressed  -> transition released->pressed THIS frame
//   Released -> transition pressed->released THIS frame
//   ConsumePress() -> mark the Pressed as claimed so it cannot fire twice.
//
// `Pressed` is intentionally consumable: gameplay actions (wall-jump, jump
// buffer, dash-jump, NPC-interact) call ConsumePress() after claiming the edge
// so a single physical press never triggers multiple actions in one frame.
class InputButton {
public:
    // Feed the raw held state for this frame and recompute transitions.
    // Call exactly once per frame (before any gameplay reads).
    void Update(bool raw_held) {
        const bool previous = down_;
        down_ = raw_held;
        pressed_ = raw_held && !previous;
        released_ = !raw_held && previous;
        press_consumed_ = false;
    }

    bool Down() const { return down_; }
    bool Pressed() const { return pressed_ && !press_consumed_; }
    bool Released() const { return released_; }

    // Claim the press edge so Pressed() returns false for the rest of the
    // frame. No-op if there is no active press.
    void ConsumePress() { press_consumed_ = true; }

    // Manually assert a press edge (used by host tests that bypass Update).
    void SetPressed() { pressed_ = true; press_consumed_ = false; }

    // Manually assert held state (used by host tests that bypass Update).
    void SetHeld() { down_ = true; }

    void Reset() {
        down_ = false;
        pressed_ = false;
        released_ = false;
        press_consumed_ = false;
    }

private:
    bool down_ = false;
    bool pressed_ = false;
    bool released_ = false;
    bool press_consumed_ = false;
};

// InputAction<Vec2> models an analog two-axis action (Move / Camera).
// The value is normalized to [-1, 1] per axis and passed through a deadzone so
// gameplay never sees stick drift / near-zero noise (§32-33).
class InputActionVec2 {
public:
    // Feed the raw per-axis values (range [-1, 1]) plus optional digital
    // cardinal contributions. Digital input is combined with the analog value.
    void Update(float raw_x, float raw_y, float deadzone = 0.15f) {
        float x = raw_x;
        float y = raw_y;
        const float length = std::sqrt((x * x) + (y * y));
        if (length < deadzone) {
            x = 0.0f;
            y = 0.0f;
        } else {
            // Rescale past the deadzone so a full stick deflection still maps
            // to unit magnitude.
            const float t = (length - deadzone) / (1.0f - deadzone);
            const float scale = t / (length > 0.0001f ? length : 1.0f);
            x *= scale;
            y *= scale;
            // Clamp the combined magnitude to 1 so a full diagonal deflection
            // (raw length up to ~sqrt(2)) never exceeds unit speed (§12: a
            // diagonal must NOT be faster than a cardinal).
            const float mag = std::sqrt((x * x) + (y * y));
            if (mag > 1.0f) {
                x /= mag;
                y /= mag;
            }
        }
        value_.x = x;
        value_.y = y;
    }

    // Add a digital cardinal contribution (e.g. D-pad): dir is one of
    // (0,-1) up, (0,+1) down, (-1,0) left, (+1,0) right.
    void AddDigital(float dx, float dy) {
        value_.x += dx;
        value_.y += dy;
        const float length = std::sqrt(
            (value_.x * value_.x) + (value_.y * value_.y));
        if (length > 1.0f) {
            value_.x /= length;
            value_.y /= length;
        }
    }

    const Vec2& Value() const { return value_; }

    void Reset() { value_ = {0.0f, 0.0f}; }

private:
    Vec2 value_;
};

}  // namespace madeline_cube
