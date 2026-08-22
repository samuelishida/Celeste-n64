#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>

#include "gameplay/math_types.hpp"

namespace madeline_cube {

// File-local bit-level float helpers, exposed (inline) so the hot XZ helpers
// below can live in the header. Moved out of math.cpp's anonymous namespace.
namespace math_detail {
inline uint32_t FloatBits(float value) {
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}
inline bool IsFiniteBits(float value) {
    return (FloatBits(value) & 0x7F800000u) != 0x7F800000u;
}
inline float FlushSubnormalBits(float value) {
    const uint32_t bits = FloatBits(value);
    const uint32_t exponent = bits & 0x7F800000u;
    const uint32_t mantissa = bits & 0x007FFFFFu;
    return exponent == 0u && mantissa != 0u ? 0.0f : value;
}
inline float Length2D(float x, float y) { return std::sqrt((x * x) + (y * y)); }
}  // namespace math_detail

// Trig LUT size (4096 entries over [0, 2π)). Single source of truth shared by
// the header's radian-based trig and the LUT table in math.cpp.
constexpr int kTrigLutSize = 4096;

// Precomputed trig LUT (4096 entries over [0, 2π)) for the hot path. Avoids
// per-frame std::sin/cos in RotateTowardXZ / DirectionFromAngle.
float SinLUT(uint16_t angle);
float CosLUT(uint16_t angle);
float LUT2Radians(uint16_t angle);  // angle -> radians

// Radian-based trig routed through the LUT. fmod to [0, 2π) first, then
// convert to a LUT index (inverse of LUT2Radians); the mask wraps the
// full-range float safely into [0, kTrigLutSize).
inline float CosRadians(float rad) {
    constexpr float kTau = 6.28318530717958647692f;
    float r = std::fmod(rad, kTau);
    if (r < 0.0f) r += kTau;
    const uint16_t index =
        static_cast<uint16_t>((r / kTau) * kTrigLutSize) & (kTrigLutSize - 1);
    return CosLUT(index);
}
inline float SinRadians(float rad) {
    constexpr float kTau = 6.28318530717958647692f;
    float r = std::fmod(rad, kTau);
    if (r < 0.0f) r += kTau;
    const uint16_t index =
        static_cast<uint16_t>((r / kTau) * kTrigLutSize) & (kTrigLutSize - 1);
    return SinLUT(index);
}

__attribute__((always_inline)) inline float Clamp(float value, float min_value, float max_value) {
    if (value < min_value) return min_value;
    if (value > max_value) return max_value;
    return value;
}

__attribute__((always_inline)) inline float Approach(float current, float target, float amount) {
    if (current < target) {
        const float next = current + amount;
        return next > target ? target : next;
    }
    const float next = current - amount;
    return next < target ? target : next;
}

float AngleApproach(float from, float to, float max_delta);
float AngleXZ(const Vec3& value);

// Highest-frequency trig path (up to 3×/frame via RotateTowardXZ from
// player_controller.cpp). Routes through the LUT instead of std::cos/std::sin.
__attribute__((always_inline)) inline Vec3 DirectionFromAngle(float angle) {
    return {CosRadians(angle), 0.0f, SinRadians(angle)};
}

__attribute__((always_inline)) inline Vec3 RotateTowardXZ(const Vec3& from, const Vec3& to, float max_delta) {
    return DirectionFromAngle(AngleApproach(AngleXZ(from), AngleXZ(to), max_delta));
}

Vec3 ApproachXZ(const Vec3& current, const Vec3& target, float amount);

// XZ-plane helpers (Y-up).
__attribute__((always_inline)) inline float LengthXZ(const Vec3& value) {
    return math_detail::Length2D(value.x, value.z);
}

float DotXZ(const Vec3& a, const Vec3& b);

// NormalizeXZ falls back to `fallback` on zero-length or non-finite input,
// preserving the controller's sanitization.
__attribute__((always_inline)) inline Vec3 NormalizeXZ(const Vec3& value, const Vec3& fallback = {0.0f, 0.0f, 1.0f}) {
    constexpr float kEpsilon = 0.0001f;
    if (!math_detail::IsFiniteBits(value.x) || !math_detail::IsFiniteBits(value.z)) {
        return fallback;
    }
    const Vec3 sanitized = {
        math_detail::FlushSubnormalBits(value.x),
        0.0f,
        math_detail::FlushSubnormalBits(value.z),
    };
    const float len = LengthXZ(sanitized);
    if (!std::isfinite(len) || len <= kEpsilon) {
        return fallback;
    }
    return {
        math_detail::FlushSubnormalBits(sanitized.x / len),
        0.0f,
        math_detail::FlushSubnormalBits(sanitized.z / len),
    };
}

// Analog stick magnitude remap (0.4 -> 0.3, 0.92 -> 1.0).
float AnalogMagnitude(float raw_length);

// Convert 2D move input into a camera-relative XZ direction.
Vec3 RelativeMoveInput(const Vec2& move, const Vec3& camera_forward, const Vec3& fallback_facing);
}
