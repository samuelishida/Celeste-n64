// Host test for ice material (§): MAT_ICE faces reduce ground friction
// (low-friction slide) — the player decelerates slower than on solid ground.
//
// Build:
//   g++ -std=c++17 -Isrc/user tests/feel_spec/ice_smoke.cpp \
//       src/user/gameplay/player/player_controller.cpp \
//       src/user/gameplay/player/player_motor.cpp \
//       src/user/gameplay/world/world.cpp \
//       src/user/gameplay/world/room_data.cpp \
//       src/user/gameplay/physics/coll_mesh.cpp \
//       src/user/gameplay/physics/geom.cpp \
//       src/user/gameplay/runtime/math.cpp -o /tmp/ice_smoke && /tmp/ice_smoke
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

    // On solid ground: no input -> decelerate at ground_deceleration.
    PlayerState solid;
    solid.grounded = true;
    solid.on_ice = false;
    solid.velocity = {config.run_speed, 0.0f, 0.0f};
    solid.target_facing = {1.0f, 0.0f, 0.0f};
    controller.Step(solid, {}, camera_forward, kDt);
    const float solid_speed = LengthXZ(solid.velocity);

    // On ice: no input -> decelerate much slower.
    PlayerState ice;
    ice.grounded = true;
    ice.on_ice = true;
    ice.velocity = {config.run_speed, 0.0f, 0.0f};
    ice.target_facing = {1.0f, 0.0f, 0.0f};
    controller.Step(ice, {}, camera_forward, kDt);
    const float ice_speed = LengthXZ(ice.velocity);

    // Ice decelerates slower: ice_speed > solid_speed after one step.
    assert(ice_speed > solid_speed);
    // Both still decelerating (speed < run_speed).
    assert(solid_speed < config.run_speed);
    assert(ice_speed < config.run_speed);

    // Ice must not affect air movement: airborne decel is unchanged.
    PlayerState air_ice;
    air_ice.grounded = false;
    air_ice.on_ice = true;
    air_ice.velocity = {config.run_speed, 0.0f, 0.0f};
    air_ice.target_facing = {1.0f, 0.0f, 0.0f};
    controller.Step(air_ice, {}, camera_forward, kDt);
    assert(LengthXZ(air_ice.velocity) > 0.0f);

    return 0;
}
