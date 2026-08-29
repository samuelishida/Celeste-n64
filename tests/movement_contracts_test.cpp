#include <cassert>

#include "../src/user/gameplay/physics_contracts.hpp"
#include "../src/user/gameplay/player/movement_config.hpp"
#include "../src/user/gameplay/player/player_state.hpp"
#include "../src/user/gameplay/player/player_controller.hpp"

using namespace madeline_cube;

int main() {
    static_assert(static_cast<int>(LocomotionState::Idle) == 0);
    static_assert(static_cast<int>(LocomotionState::Run) == 1);
    static_assert(static_cast<int>(LocomotionState::Jump) == 2);
    static_assert(static_cast<int>(LocomotionState::Dash) == 3);
    static_assert(static_cast<int>(LocomotionState::Climb) == 4);
    static_assert(static_cast<int>(LocomotionState::Fall) == 5);

    // MovementProfile deleted (review S4/C1): feel constants live in
    // MovementConfig alone — pin the OG values on the one source of truth.
    MovementConfig config;
    assert(config.coyote_time == 0.12f);
    assert(config.jump_buffer_time == 0.10f);   // OG device buffer (was 0.08)
    assert(config.dash_hitstop_time == 0.02f);
    assert(config.dash_active_time >= 0.18f && config.dash_active_time <= 0.22f);

    // dash_momentum is an inter-phase communication field on StepContext,
    // not on PlayerState. Verify it defaults to zero.
    PlayerController::StepContext ctx;
    assert(ctx.dash_momentum.x == 0.0f);
    assert(ctx.dash_momentum.y == 0.0f);
    assert(ctx.dash_momentum.z == 0.0f);

    // PlayerState should not have dash_momentum (it moved to StepContext).
    PlayerState player;
    assert(player.locomotion_state == LocomotionState::Idle);

    // CameraBasis removed (review C7): zero prod consumers after the motor
    // deletion; the camera reads the room scene-side.
    return 0;
}