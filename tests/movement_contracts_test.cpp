#include <cassert>

#include "../src/user/gameplay/physics_contracts.hpp"
#include "../src/user/gameplay/player/movement_config.hpp"
#include "../src/user/gameplay/player/player_state.hpp"

using namespace madeline_cube;

int main() {
    static_assert(static_cast<int>(LocomotionState::Idle) == 0);
    static_assert(static_cast<int>(LocomotionState::Run) == 1);
    static_assert(static_cast<int>(LocomotionState::Jump) == 2);
    static_assert(static_cast<int>(LocomotionState::Dash) == 3);
    static_assert(static_cast<int>(LocomotionState::Climb) == 4);
    static_assert(static_cast<int>(LocomotionState::Fall) == 5);

    MovementProfile profile;
    assert(profile.coyote_time == 0.15f);
    assert(profile.jump_buffer_time == 0.10f);
    assert(profile.dash_hitstop_time == 0.05f);
    assert(profile.dash_active_time >= 0.15f && profile.dash_active_time <= 0.20f);

    PlayerState player;
    assert(player.locomotion_state == LocomotionState::Idle);
    assert(player.stamina == profile.stamina_max);
    assert(!player.climb_exhausted);

    CameraBasis camera;
    assert(camera.forward_xz.z == 1.0f);
    assert(camera.right_xz.x == 1.0f);
    return 0;
}
