#pragma once

#include "gameplay/player/movement_config.hpp"
#include "gameplay/player/player_movement.hpp"
#include "gameplay/player/player_state.hpp"
#include "gameplay/world/world.hpp"

namespace madeline_cube {

class RespawnSystem {
public:
    explicit RespawnSystem(MovementConfig config = {});

    // Player-state-only respawn used by host smoke tests and legacy callers.
    // Restores spawn point + zero velocity + cleared timers. Does NOT resolve
    // contacts because no room or mover is provided.
    bool Step(PlayerState& player, const Vec3& checkpoint) const;

    // Full respawn that resolves contact via the single mover (MoveService).
    // Restores spawn point + zero velocity + cleared timers, then runs the
    // OG ground snap so the resulting grounded/contact state is owned by the
    // same mover the gameplay tick uses.
    // When death_triggered is true, respawns regardless of fall height.
    bool Step(
        PlayerState& player,
        const Vec3& checkpoint,
        const Room& room,
        const MoveService& mover,
        bool death_triggered = false
    ) const;

private:
    MovementConfig config_;

    // Shared helper: writes spawn point, zeroes velocity, clears timers and
    // gameplay flags. Leaves contact resolution to the caller.
    void ResetState(PlayerState& player, const Vec3& checkpoint) const;
};

}  // namespace madeline_cube

