#include <cassert>

#include "gameplay/player/player_controller.hpp"

using namespace madeline_cube;

int main() {
    constexpr float kDt = 1.0f / 60.0f;
    const Vec3 camera_forward{0.0f, 0.0f, 1.0f};
    PlayerController controller;
    PlayerState player;
    player.facing = {0.0f, 0.0f, 1.0f};
    player.target_facing = player.facing;
    player.last_facing = player.facing;
    player.grounded = false;

    PlayerInput dash;
    dash.move = {1.0f, 0.0f};
    dash.dash_pressed = true;
    controller.Step(player, dash, camera_forward, kDt);

    assert(player.movement_state == PlayerMovementState::Dashing);
    const float facing_dot = player.facing.x * player.target_facing.x +
                             player.facing.z * player.target_facing.z;
    assert(facing_dot > 0.999f);
    return 0;
}
