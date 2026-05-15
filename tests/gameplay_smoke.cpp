#include <cassert>

#include "../src/user/gameplay/collectible.hpp"
#include "../src/user/gameplay/player_controller.hpp"
#include "../src/user/gameplay/respawn_system.hpp"

using namespace madeline_cube;

int main() {
    MovementConfig config;
    PlayerController controller(config);
    RespawnSystem respawn(config);

    PlayerState player;
    player.grounded = true;

    PlayerInput jump_input;
    jump_input.jump_pressed = true;
    controller.Step(player, jump_input, 1.0f / 60.0f);
    assert(!player.grounded);
    assert(player.velocity.y > 0.0f);

    PlayerInput dash_input;
    dash_input.move = {1.0f, 0.0f};
    dash_input.dash_pressed = true;
    controller.Step(player, dash_input, 1.0f / 60.0f);
    assert(!player.air_dash_available);
    assert(player.velocity.x > 0.0f);

    CollectibleState collectible;
    collectible.position = player.position;
    assert(TryCollect(collectible, player.position));
    assert(collectible.collected);

    player.position.y = config.respawn_fall_height - 1.0f;
    const Vec3 checkpoint = {0.0f, 2.0f, 0.0f};
    assert(respawn.Step(player, checkpoint));
    assert(player.position.y == checkpoint.y);
    assert(player.velocity.x == 0.0f);
    assert(player.air_dash_available);

    return 0;
}

