#include <cassert>
#include <cmath>

#include "gameplay/player/player_controller.hpp"
#include "gameplay/render/player_hair_color.hpp"
#include "gameplay/render/player_hair_node_count.hpp"

using namespace madeline_cube;

int main() {
    const Vec3 camera_forward{0.0f, 0.0f, 1.0f};
    PlayerController controller;

    PlayerState fresh;
    assert(fresh.dashes == 1);

    // OG TryDash spends one charge per accepted dash, so two-charge state can
    // dash twice and then must reject a third press.
    PlayerState player;
    player.grounded = false;
    player.dashes = 2;
    const PlayerInput no_dash_input;
    auto wait_until_dash_ready = [&]() {
        for (int tick = 0; tick < 120; ++tick) {
            if (player.movement_state == PlayerMovementState::Normal &&
                player.dash_cooldown_remaining <= 0.0f &&
                player.world_hitstop_remaining <= 0.0f) {
                return;
            }
            controller.Step(player, no_dash_input, camera_forward, 1.0f / 60.0f);
        }
        assert(false && "dash must return to normal and clear cooldown");
    };
    PlayerInput first_dash;
    first_dash.move = {1.0f, 0.0f};
    first_dash.dash_pressed = true;
    controller.Step(player, first_dash, camera_forward, 1.0f / 60.0f);
    assert(player.dashes == 1);
    assert(player.movement_state == PlayerMovementState::Dashing);
    assert(std::fabs(player.world_hitstop_remaining - 0.02f) < 1.0e-6f);

    wait_until_dash_ready();
    PlayerInput second_dash;
    second_dash.move = {1.0f, 0.0f};
    second_dash.dash_pressed = true;
    controller.Step(player, second_dash, camera_forward, 1.0f / 60.0f);
    assert(player.dashes == 0);
    assert(player.movement_state == PlayerMovementState::Dashing);

    wait_until_dash_ready();
    PlayerInput third_dash;
    third_dash.move = {1.0f, 0.0f};
    third_dash.dash_pressed = true;
    controller.Step(player, third_dash, camera_forward, 1.0f / 60.0f);
    assert(player.dashes == 0);
    assert(player.movement_state == PlayerMovementState::Normal);

    // Climb's alternate dash acceptance path must spend the same charge.
    PlayerState climber;
    climber.movement_state = PlayerMovementState::Climbing;
    climber.dashes = 1;
    PlayerInput climb_dash;
    climb_dash.climb_held = true;
    climb_dash.dash_pressed = true;
    controller.Step(climber, climb_dash, camera_forward, 1.0f / 60.0f);
    assert(climber.dashes == 0);
    assert(climber.movement_state == PlayerMovementState::Dashing);

    // RefillDash(amount) raises but never lowers charges, and only a real
    // refill starts OG's 0.05 s flash.
    PlayerState refill;
    refill.dashes = 0;
    assert(controller.RefillDash(refill, 2));
    assert(refill.dashes == 2);
    assert(std::fabs(refill.dash_refill_flash_remaining - 0.05f) < 1.0e-6f);
    refill.dash_refill_flash_remaining = 0.0f;
    assert(!controller.RefillDash(refill, 1));
    assert(refill.dashes == 2);
    assert(refill.dash_refill_flash_remaining == 0.0f);

    // Ground and spring refill to at least one charge without destroying a
    // retained second charge. Ordinary grounded refill flashes only at zero.
    PlayerState grounded;
    grounded.grounded = true;
    grounded.dashes = 0;
    PlayerInput no_input;
    controller.Step(grounded, no_input, camera_forward, 1.0f / 60.0f);
    assert(grounded.dashes == 1);
    assert(std::fabs(grounded.dash_refill_flash_remaining - 0.05f) < 1.0e-6f);

    PlayerState spring;
    spring.dashes = 2;
    controller.Spring(spring, {});
    assert(spring.dashes == 2);

    assert(ResolvePlayerHairColor(2, 0.0f) == PlayerHairColorState::TwoDashes);
    assert(ResolvePlayerHairColor(0, 0.0f) == PlayerHairColorState::NoDash);
    assert(ResolvePlayerHairColor(2, 0.01f) == PlayerHairColorState::RefillFlash);
    assert(ResolvePlayerHairNodeCount(0) == 10);
    assert(ResolvePlayerHairNodeCount(2) == 16);
    assert(ResolvePlayerHairNodeCount(0, true) == 18);
    return 0;
}
