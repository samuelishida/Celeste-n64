#include <cassert>
#include <cmath>

#include "../../src/user/gameplay/player/player_controller.hpp"
#include "../../src/user/gameplay/world/world.hpp"

using namespace madeline_cube;

int main() {
    constexpr float dt = 1.0f / 60.0f;
    PlayerController controller;
    Room room;
    room.colliders[room.collider_count++] = {
        .type = ColliderType::Plane,
        .bounds = {.min = {-100.0f, 0.0f, -20.0f},
                   .max = {2.0f, 0.0f, 20.0f}},
        .solid = true,
        .normal = {0.0f, 1.0f, 0.0f},
        .face_id = 1,
    };

    PlayerState player;
    player.position = {0.0f, 11.25f, 0.0f};
    player.grounded = true;
    player.contact.was_grounded = true;
    const Vec3 camera_forward = {0.0f, 0.0f, 1.0f};

    PlayerInput run;
    run.move = {1.0f, 0.0f};
    for (int tick = 0; tick < 120 && player.grounded; ++tick) {
        controller.Step(player, run, camera_forward, dt, &room);
    }
    assert(!player.grounded);
    assert(player.position.x > 2.0f);
    assert(player.coyote_time_remaining > 0.0f);
    const float takeoff_height = player.contact.coyote_height;

    // Delay until late in OG's 0.12 s coyote window while still airborne.
    for (int tick = 0; tick < 4; ++tick) {
        controller.Step(player, {}, camera_forward, dt, &room);
    }
    assert(!player.grounded);
    assert(player.coyote_time_remaining > 0.0f &&
           player.coyote_time_remaining <= 0.06f);

    PlayerInput late_jump;
    late_jump.jump_pressed = true;
    late_jump.jump_held = true;
    controller.BeginMoveTick(player);
    auto context = controller.TimerInputPhase(player, late_jump,
                                              camera_forward, dt);
    controller.StatePhase(player, late_jump, context, dt, &room);
    assert(std::fabs(player.position.y - takeoff_height) < 0.001f);
    assert(player.velocity.y > 80.0f);
    assert(player.coyote_time_remaining == 0.0f);
    assert(!player.grounded);
    controller.MovePhase(player, dt, &room, &context);
    assert(player.position.y > takeoff_height);
    assert(!player.grounded);
    return 0;
}
