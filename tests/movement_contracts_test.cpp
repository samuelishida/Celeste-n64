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

    MovementProfile profile;
    assert(profile.coyote_time == 0.12f);
    assert(profile.jump_buffer_time == 0.08f);
    assert(profile.dash_hitstop_time == 0.02f);
    assert(profile.dash_active_time >= 0.18f && profile.dash_active_time <= 0.22f);

    // dash_momentum is an inter-phase communication field on StepContext,
    // not on PlayerState. Verify it defaults to zero.
    PlayerController::StepContext ctx;
    assert(ctx.dash_momentum.x == 0.0f);
    assert(ctx.dash_momentum.y == 0.0f);
    assert(ctx.dash_momentum.z == 0.0f);

    // PlayerState should not have dash_momentum (it moved to StepContext).
    PlayerState player;
    assert(player.locomotion_state == LocomotionState::Idle);

    CameraBasis camera;
    assert(camera.forward_xz.z == 1.0f);
    assert(camera.right_xz.x == 1.0f);
    return 0;
}