#pragma once

#include "gameplay/physics_contracts.hpp"
#include "gameplay/player/player_state.hpp"
#include "gameplay/world/world.hpp"

namespace madeline_cube {

struct PlayerMotorConfig {
    float half_height = 10.0f;
    float radius = 5.0f;
    float sweep_step = 5.0f;
    // Climb-state collision radius (climb-parity-3 Inc 1/2): OG's climb popout
    // radius IS WallPushoutDist (3). With the climb entry anchor placing the
    // waist exactly at wall.Point + normal*3, a 3.0 climb sphere makes the
    // anchor a zero-popout equilibrium; radius 5.0 otherwise (Normal-state),
    // a demake Normal-state convenience.
    float climb_collision_radius = 3.0f;
    float ground_contact_tolerance = 0.5f;
    float ground_snap_distance = 50.0f;
    float platform_carry_storage_time = 0.1f;
    // wall_probe_center_offset DELETED (climb-parity-3 Inc 2) together with
    // RunWallFlagFromWaist — wall_contact is body-sphere physical contact
    // only; climb entry uses its own TryClimb waist query (OG model).
};

// Single owner of resolved post-move position/contact. State code requests
// motion via MotorInput; the motor returns a MotorResult and writes the
// resolved fields back into PlayerState. No other system performs collision
// repair after the motor returns.
class PlayerMotor {
public:
    explicit PlayerMotor(PlayerMotorConfig config = {});

    MotorResult Step(PlayerState& state, const Room& room, const MotorInput& input, float delta_seconds) const;
    MotorResult RefreshContacts(PlayerState& state, const Room& room, const MotorInput& input) const;

    const PlayerMotorConfig& Config() const { return config_; }

private:
    PlayerMotorConfig config_;
};

}  // namespace madeline_cube
