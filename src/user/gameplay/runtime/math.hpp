#pragma once

#include <cstdint>

#include "gameplay/math_types.hpp"

namespace madeline_cube {
float Clamp(float value, float min_value, float max_value);
float Approach(float current, float target, float amount);
float AngleApproach(float from, float to, float max_delta);
float AngleXZ(const Vec3& value);
Vec3 DirectionFromAngle(float angle);
Vec3 RotateTowardXZ(const Vec3& from, const Vec3& to, float max_delta);
Vec3 ApproachXZ(const Vec3& current, const Vec3& target, float amount);

// XZ-plane helpers (Y-up). NormalizeXZ falls back to `fallback` on
// zero-length or non-finite input, preserving the controller's sanitization.
float LengthXZ(const Vec3& value);
float DotXZ(const Vec3& a, const Vec3& b);
Vec3 NormalizeXZ(const Vec3& value, const Vec3& fallback = {0.0f, 0.0f, 1.0f});

// Analog stick magnitude remap (0.4 -> 0.3, 0.92 -> 1.0).
float AnalogMagnitude(float raw_length);

// Convert 2D move input into a camera-relative XZ direction.
Vec3 RelativeMoveInput(const Vec2& move, const Vec3& camera_forward, const Vec3& fallback_facing);

// Precomputed trig LUT (4096 entries over [0, 2π)) for the hot path. Avoids
// per-frame std::sin/cos in RotateTowardXZ / DirectionFromAngle.
float SinLUT(uint16_t angle);
float CosLUT(uint16_t angle);
float LUT2Radians(uint16_t angle);  // angle -> radians
}
