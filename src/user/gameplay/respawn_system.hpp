#pragma once

#include "movement_config.hpp"
#include "player_motor.hpp"
#include "player_state.hpp"
#include "world.hpp"

namespace madeline_cube {

class RespawnSystem {
public:
    explicit RespawnSystem(MovementConfig config = {});

    // Player-state-only respawn used by host smoke tests and legacy callers.
    // Restores spawn point + zero velocity + cleared timers. Does NOT resolve
    // contacts because no room or motor is provided.
    bool Step(PlayerState& player, const Vec3& checkpoint) const;

    // Full respawn that delegates contact resolution to the motor.
    // Restores spawn point + zero velocity + cleared timers, then calls
    // PlayerMotor::RefreshContacts so the motor is the single owner of the
    // resulting grounded/contact state.
    bool Step(
        PlayerState& player,
        const Vec3& checkpoint,
        const Room& room,
        const PlayerMotor& motor
    ) const;

private:
    MovementConfig config_;

    // Shared helper: writes spawn point, zeroes velocity, clears timers and
    // gameplay flags. Leaves contact resolution to the caller.
    void ResetState(PlayerState& player, const Vec3& checkpoint) const;
};

}  // namespace madeline_cube

