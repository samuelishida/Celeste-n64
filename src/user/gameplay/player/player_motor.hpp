#pragma once

#include "gameplay/physics_contracts.hpp"
#include "gameplay/player/player_state.hpp"
#include "gameplay/world/world.hpp"

namespace madeline_cube {

struct PlayerMotorConfig {
    float half_height = 1.0f;
    float radius = 0.5f;
    float sweep_step = 0.5f;
    float ground_contact_tolerance = 0.05f;
    float ground_snap_distance = 5.0f;
    float platform_carry_storage_time = 0.1f;
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
