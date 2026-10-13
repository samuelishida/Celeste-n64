#include <cassert>

#include "../../src/user/gameplay/player/player_controller.hpp"

using namespace madeline_cube;

int main() {
    MovementConfig config;
    PlayerController controller(config);
    constexpr float dt = 1.0f / 60.0f;

    PlayerState coyote;
    coyote.grounded = false;
    coyote.coyote_time_remaining = 0.15f;
    controller.Step(coyote, {.jump_pressed = true, .jump_held = true}, {0,0,1}, dt);
    assert(coyote.velocity.y > 0.0f);

    PlayerState buffered;
    buffered.grounded = false;
    controller.Step(buffered, {.jump_pressed = true, .jump_held = true}, {0,0,1}, dt);
    assert(buffered.velocity.y <= 0.0f);  // airborne, no coyote: buffered, not fired
    buffered.grounded = true;
    buffered.coyote_time_remaining = config.coyote_time;  // LateUpdate support refresh
    buffered.contact.coyote_height = buffered.position.y;
    // Inc 1: the device keeps the pressed edge live for the 0.10 s buffer
    // window, so the second tick replays the same buffered press.
    controller.Step(buffered, {.jump_pressed = true, .jump_held = true}, {0,0,1}, dt);
    assert(buffered.velocity.y > 0.0f);   // buffered edge consumed on landing

    PlayerState full;
    full.grounded = true;
    full.coyote_time_remaining = config.coyote_time;
    full.contact.coyote_height = full.position.y;
    controller.Step(full, {.jump_pressed = true, .jump_held = true}, {0,0,1}, dt);
    PlayerState short_hop = full;
    controller.Step(full, {.jump_held = true}, {0,0,1}, dt);
    controller.Step(short_hop, {}, {0,0,1}, dt);
    assert(short_hop.velocity.y < full.velocity.y);

    PlayerState rise;
    rise.velocity.y = 50.0f;
    controller.Step(rise, {.jump_held = true}, {0,0,1}, dt);
    PlayerState apex;
    apex.velocity.y = 5.0f;
    controller.Step(apex, {.jump_held = true}, {0,0,1}, dt);
    assert((50.0f - rise.velocity.y) < (5.0f - apex.velocity.y));
    return 0;
}
