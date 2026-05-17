#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>

#include "math_types.hpp"
#include "gameplay/player/player_state.hpp"
#include "gameplay/world/world.hpp"

namespace madeline_cube {

namespace {

inline uint32_t FloatBits(float value) {
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

}  // namespace

// ---------------------------------------------------------------------------
// Coordinate system adapter
//
// The C# source uses +Z up (Celeste64 convention).
// The N64 port uses +Y up (libdragon/tiny3d convention).
// All adapter functions are explicit and tested; gameplay code stays in port
// coordinates except at capture/replay boundaries.
// ---------------------------------------------------------------------------

inline Vec3 SrcToPort(const Vec3& src) {
    return {src.x, src.z, -src.y};
}

inline Vec3 PortToSrc(const Vec3& port) {
    return {port.x, -port.z, port.y};
}

inline Vec2 SrcToPort(const Vec2& src) {
    return {src.x, src.y};
}

inline Vec2 PortToSrc(const Vec2& port) {
    return {port.x, port.y};
}

// ---------------------------------------------------------------------------
// Motor contract
//
// State code requests motion; the motor alone writes resolved post-move
// position/contact results.  These structs are the boundary.
// ---------------------------------------------------------------------------

struct MotorInput {
    Vec3 requested_velocity;
    bool wants_ground_snap = true;
    bool wants_coyote_refresh = true;
    bool wants_dash_refill = true;
};

struct MotorResult {
    Vec3 position;
    Vec3 velocity;
    bool grounded = false;
    bool wall_contact = false;
    bool landed_this_frame = false;
    int ground_face_id = -1;
    int wall_face_id = -1;
    Vec3 ground_normal = {0.0f, 1.0f, 0.0f};
    Vec3 wall_normal = {0.0f, 0.0f, 0.0f};
};

struct CameraBasis {
    Vec3 forward_xz = {0.0f, 0.0f, 1.0f};
    Vec3 right_xz = {1.0f, 0.0f, 0.0f};
};

// ---------------------------------------------------------------------------
// Fixture helpers
//
// Deterministic tie-breaking and numeric validity checks used by tests
// and ROM diagnostics alike.
// ---------------------------------------------------------------------------

inline bool IsNumericValid(float value) {
    // Reject NaN, +inf, -inf, and subnormal floats.
    const uint32_t bits = FloatBits(value);
    const uint32_t exponent = bits & 0x7F800000u;
    if (exponent == 0x7F800000u) {
        return false;  // inf or NaN
    }
    if (exponent == 0u && (bits & 0x007FFFFFu) != 0u) {
        return false;  // subnormal
    }
    return true;
}

inline bool IsNumericValid(const Vec3& v) {
    return IsNumericValid(v.x) && IsNumericValid(v.y) && IsNumericValid(v.z);
}

inline bool NearlyEqual(float a, float b, float eps = 0.0001f) {
    return std::fabs(a - b) <= eps;
}

}  // namespace madeline_cube
