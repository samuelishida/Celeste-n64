#include "gameplay/runtime/math.hpp"
#include <cmath>
namespace madeline_cube {
float Approach(float current, float target, float amount) {
    if (current < target) {
        const float next = current + amount;
        return next > target ? target : next;
    }
    const float next = current - amount;
    return next < target ? target : next;
}
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
Vec3 DirectionFromAngle(float angle) { return {std::cos(angle), 0.0f, std::sin(angle)}; }
Vec3 RotateTowardXZ(const Vec3& from, const Vec3& to, float max_delta) {
    return DirectionFromAngle(AngleApproach(AngleXZ(from), AngleXZ(to), max_delta));
}
Vec3 ApproachXZ(const Vec3& current, const Vec3& target, float amount) {
    const float dx = target.x - current.x;
    const float dz = target.z - current.z;
    const float distance = std::sqrt(dx * dx + dz * dz);
    if (distance <= amount || distance <= 0.0001f) return {target.x, current.y, target.z};
    const float scale = amount / distance;
    return {current.x + dx * scale, current.y, current.z + dz * scale};
}
}
