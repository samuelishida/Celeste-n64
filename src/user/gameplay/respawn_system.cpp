#include "respawn_system.hpp"

namespace madeline_cube {

RespawnSystem::RespawnSystem(MovementConfig config) : config_(config) {}

bool RespawnSystem::Step(PlayerState& player, const Vec3& checkpoint) const {
    if (player.position.y >= config_.respawn_fall_height) {
        return false;
    }

    player.position = checkpoint;
    player.velocity = {};
    player.grounded = false;
    player.air_dash_available = true;
    player.dash_time_remaining = 0.0f;
    player.dash_cooldown_remaining = 0.0f;
    player.dash_reset_cooldown_remaining = 0.0f;
    player.no_dash_jump_remaining = 0.0f;
    player.movement_state = PlayerMovementState::Normal;
    player.wall_grabbing = false;
    player.hold_jump_time_remaining = 0.0f;
    player.auto_jump = false;
    return true;
}

}  // namespace madeline_cube
