#include "gameplay/runtime/math.hpp"
#include <cmath>
#include <cstdint>
#include <cstring>
namespace madeline_cube {
namespace {
constexpr float kEpsilon = 0.0001f;
}  // namespace

float AngleApproach(float from, float to, float max_delta) {
    constexpr float kPi = 3.14159265358979323846f;
    constexpr float kTau = kPi * 2.0f;
    float diff = std::fmod(to - from + kPi, kTau);
    if (diff < 0.0f) diff += kTau;
    diff -= kPi;
    if (diff > max_delta) diff = max_delta;
    if (diff < -max_delta) diff = -max_delta;
    return from + diff;
}
float AngleXZ(const Vec3& value) { return std::atan2(value.z, value.x); }
Vec3 ApproachXZ(const Vec3& current, const Vec3& target, float amount) {
    const float dx = target.x - current.x;
    const float dz = target.z - current.z;
    const float distance = std::sqrt(dx * dx + dz * dz);
    if (distance <= amount || distance <= 0.0001f) return {target.x, current.y, target.z};
    const float scale = amount / distance;
    return {current.x + dx * scale, current.y, current.z + dz * scale};
}

float DotXZ(const Vec3& a, const Vec3& b) { return (a.x * b.x) + (a.z * b.z); }

float AnalogMagnitude(float raw_length) {
    const float t = Clamp((raw_length - 0.4f) / (0.92f - 0.4f), 0.0f, 1.0f);
    return 0.3f + ((1.0f - 0.3f) * t);
}

Vec3 RelativeMoveInput(const Vec2& move, const Vec3& camera_forward, const Vec3& fallback_facing) {
    const float input_length = math_detail::Length2D(move.x, move.y);
    if (input_length <= kEpsilon) {
        return {};
    }

    const Vec3 forward = NormalizeXZ(camera_forward, fallback_facing);
    const Vec3 right = {forward.z, 0.0f, -forward.x};
    const float normalized_x = move.x / input_length;
    const float normalized_y = move.y / input_length;
    return NormalizeXZ({
        (right.x * normalized_x) + (forward.x * normalized_y),
        0.0f,
        (right.z * normalized_x) + (forward.z * normalized_y),
    }, fallback_facing);
}

// Trig LUT: 4096 entries over [0, 2π). Angle is uint16_t wrapping, so
// SinLUT(1024) == sin(π/2) == 1.0. Deterministic and fast.
namespace {
struct TrigLUT {
    float sin[kTrigLutSize];
    float cos[kTrigLutSize];
    TrigLUT() {
        constexpr float kTau = 6.28318530717958647692f;
        for (int i = 0; i < kTrigLutSize; ++i) {
            const float a = (static_cast<float>(i) / kTrigLutSize) * kTau;
            sin[i] = std::sin(a);
            cos[i] = std::cos(a);
        }
    }
};
const TrigLUT& GetLUT() {
    static const TrigLUT lut;
    return lut;
}
}  // namespace

float LUT2Radians(uint16_t angle) {
    constexpr float kTau = 6.28318530717958647692f;
    return (static_cast<float>(angle) / kTrigLutSize) * kTau;
}

float SinLUT(uint16_t angle) { return GetLUT().sin[angle & (kTrigLutSize - 1)]; }
float CosLUT(uint16_t angle) { return GetLUT().cos[angle & (kTrigLutSize - 1)]; }
}

