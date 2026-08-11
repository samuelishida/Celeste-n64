// Host test for slope speed multiplier (§42): ground movement speed is scaled
// by ground_normal (flat 1.0, downhill >1.0, uphill <1.0, clamped).
//
// Build:
//   g++ -std=c++17 -Isrc/user tests/feel_spec/slope_smoke.cpp \
//       src/user/gameplay/player/player_controller.cpp \
//       src/user/gameplay/runtime/math.cpp -o /tmp/slope_smoke && /tmp/slope_smoke
#include <cassert>
#include <cmath>

#include "../../src/user/gameplay/player/player_controller.hpp"

using namespace madeline_cube;

namespace {
constexpr float kDt = 1.0f / 60.0f;
constexpr float kSqrtHalf = 0.70710678f;
float LengthXZ(const Vec3& v) { return std::sqrt(v.x * v.x + v.z * v.z); }
}

int main() {
    MovementConfig config;
    PlayerController controller(config);
    const Vec3 camera_forward = {0.0f, 0.0f, 1.0f};

    // Flat ground: normal {0,1,0} -> multiplier 1.0.
    PlayerState flat;
    flat.grounded = true;
    flat.contact.ground_normal = {0.0f, 1.0f, 0.0f};
    flat.velocity = {0.0f, 0.0f, 0.0f};
    PlayerInput fwd;
    fwd.move = {1.0f, 0.0f};
    for (int i = 0; i < 30; ++i) controller.Step(flat, fwd, camera_forward, kDt);
    // Flat approaches run_max_speed (64).
    assert(flat.velocity.x > 0.0f);
    assert(flat.velocity.x <= config.run_speed + 0.01f);

    // Downhill: normal leans forward (+X), moving +X -> multiplier > 1.0.
    PlayerState down;
    down.grounded = true;
    down.contact.ground_normal = {kSqrtHalf, kSqrtHalf, 0.0f};
    down.velocity = {0.0f, 0.0f, 0.0f};
    for (int i = 0; i < 30; ++i) controller.Step(down, fwd, camera_forward, kDt);
    assert(down.velocity.x > flat.velocity.x);  // downhill faster than flat

    // Uphill: normal leans backward (-X), moving +X -> multiplier < 1.0.
    PlayerState up;
    up.grounded = true;
    up.contact.ground_normal = {-kSqrtHalf, kSqrtHalf, 0.0f};
    up.velocity = {0.0f, 0.0f, 0.0f};
    for (int i = 0; i < 30; ++i) controller.Step(up, fwd, camera_forward, kDt);
    assert(up.velocity.x < flat.velocity.x);  // uphill slower than flat

    // Near-vertical normal (wall) must not apply slope scaling.
    PlayerState wall;
    wall.grounded = true;
    wall.contact.ground_normal = {1.0f, 0.1f, 0.0f};
    wall.velocity = {0.0f, 0.0f, 0.0f};
    controller.Step(wall, fwd, camera_forward, kDt);
    assert(wall.velocity.x > 0.0f);

    return 0;
}
