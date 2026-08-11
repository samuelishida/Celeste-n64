// Host test for skid state (§16): running fast and pushing opposite direction
// enters a skid sub-state that preserves momentum, decelerates, then
// accelerates toward the new direction; a skid jump is possible.
//
// Build:
//   g++ -std=c++17 -Isrc/user tests/feel_spec/skid_smoke.cpp \
//       src/user/gameplay/player/player_controller.cpp \
//       src/user/gameplay/runtime/math.cpp -o /tmp/skid_smoke && /tmp/skid_smoke
#include <cassert>
#include <cmath>

#include "../../src/user/gameplay/player/player_controller.hpp"

using namespace madeline_cube;

namespace {
constexpr float kDt = 1.0f / 60.0f;
float LengthXZ(const Vec3& v) { return std::sqrt(v.x * v.x + v.z * v.z); }
}

int main() {
    MovementConfig config;
    PlayerController controller(config);
    const Vec3 camera_forward = {0.0f, 0.0f, 1.0f};

    // Run right fast, then push left -> skidding true, momentum preserved.
    PlayerState skid;
    skid.grounded = true;
    skid.velocity = {config.run_speed, 0.0f, 0.0f};
    skid.target_facing = {1.0f, 0.0f, 0.0f};
    PlayerInput reverse;
    reverse.move = {-1.0f, 0.0f};
    controller.Step(skid, reverse, camera_forward, kDt);
    assert(skid.skidding);
    // Momentum preserved: still moving right (positive x), not instantly inverted.
    assert(skid.velocity.x > 0.0f);
    assert(skid.velocity.x <= config.run_speed);

    // Skid jump: pressing jump during skid leaves the ground and jumps.
    PlayerState skid_jump;
    skid_jump.grounded = true;
    skid_jump.velocity = {config.run_speed, 0.0f, 0.0f};
    skid_jump.target_facing = {1.0f, 0.0f, 0.0f};
    PlayerInput reverse_jump;
    reverse_jump.move = {-1.0f, 0.0f};
    reverse_jump.jump_pressed = true;
    reverse_jump.jump_held = true;
    controller.Step(skid_jump, reverse_jump, camera_forward, kDt);
    assert(skid_jump.skidding);
    assert(skid_jump.velocity.y > 0.0f);  // jumped

    // Gentle direction change must NOT trigger skid.
    PlayerState gentle;
    gentle.grounded = true;
    gentle.velocity = {config.run_speed, 0.0f, 0.0f};
    gentle.target_facing = {1.0f, 0.0f, 0.0f};
    PlayerInput slight;
    slight.move = {0.5f, 0.0f};  // same general direction
    controller.Step(gentle, slight, camera_forward, kDt);
    assert(!gentle.skidding);

    // Standing player (velocity ~0) must not NaN / skid.
    PlayerState standing;
    standing.grounded = true;
    standing.velocity = {0.0f, 0.0f, 0.0f};
    PlayerInput left;
    left.move = {-1.0f, 0.0f};
    controller.Step(standing, left, camera_forward, kDt);
    assert(!standing.skidding);
    assert(std::isfinite(standing.velocity.x));

    return 0;
}
