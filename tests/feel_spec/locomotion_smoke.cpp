#include <cassert>
#include <cmath>

#include "../../src/user/gameplay/player/player_controller.hpp"

using namespace madeline_cube;

namespace {
float LengthXZ(const Vec3& value) { return std::sqrt(value.x * value.x + value.z * value.z); }
}

int main() {
    PlayerController controller;
    constexpr float dt = 1.0f / 60.0f;

    PlayerState stop;
    stop.grounded = true;
    stop.velocity = {64.0f, 0.0f, 0.0f};
    for (int i = 0; i < 6; ++i) controller.Step(stop, {}, {0.0f, 0.0f, 1.0f}, dt);
    assert(LengthXZ(stop.velocity) < 0.1f);

    PlayerState turn;
    turn.grounded = true;
    turn.velocity = {64.0f, 0.0f, 0.0f};
    turn.target_facing = {1.0f, 0.0f, 0.0f};
    PlayerInput reverse;
    reverse.move = {-1.0f, 0.0f};
    for (int i = 0; i < 3; ++i) controller.Step(turn, reverse, {0.0f, 0.0f, 1.0f}, dt);
    assert(turn.target_facing.x < -0.9f);
    assert(turn.movement_state == PlayerMovementState::Normal);

    PlayerState air;
    air.grounded = false;
    air.velocity = {64.0f, 0.0f, 0.0f};
    const float before = LengthXZ(air.velocity);
    controller.Step(air, reverse, {0.0f, 0.0f, 1.0f}, dt);
    assert(LengthXZ(air.velocity) > 0.0f);
    assert(LengthXZ(air.velocity) < before);
    return 0;
}
