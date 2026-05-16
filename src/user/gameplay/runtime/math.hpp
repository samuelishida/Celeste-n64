#pragma once

#include "gameplay/math_types.hpp"

namespace madeline_cube {
float Approach(float current, float target, float amount);
float AngleApproach(float from, float to, float max_delta);
float AngleXZ(const Vec3& value);
Vec3 DirectionFromAngle(float angle);
Vec3 RotateTowardXZ(const Vec3& from, const Vec3& to, float max_delta);
Vec3 ApproachXZ(const Vec3& current, const Vec3& target, float amount);
}
