#include <cassert>

#include "../../src/user/gameplay/player/player_controller.hpp"

using namespace madeline_cube;

int main() {
    PlayerController controller;
    constexpr float dt = 1.0f / 60.0f;

    PlayerState coyote;
    coyote.grounded = false;
    coyote.coyote_time_remaining = 0.15f;
    controller.Step(coyote, {.jump_pressed = true, .jump_held = true}, {0,0,1}, dt);
    assert(coyote.velocity.y > 0.0f);

    PlayerState buffered;
    buffered.grounded = false;
    controller.Step(buffered, {.jump_pressed = true, .jump_held = true}, {0,0,1}, dt);
    assert(buffered.jump_buffer_remaining > 0.06f);
    buffered.grounded = true;
    controller.Step(buffered, {.jump_held = true}, {0,0,1}, dt);
    assert(buffered.velocity.y > 0.0f);
    assert(buffered.jump_buffer_remaining == 0.0f);

    PlayerState full;
    full.grounded = true;
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
