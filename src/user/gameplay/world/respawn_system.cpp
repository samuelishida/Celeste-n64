#include "gameplay/world/respawn_system.hpp"

namespace madeline_cube {

RespawnSystem::RespawnSystem(MovementConfig config) : config_(config) {}

void RespawnSystem::ResetState(PlayerState& player, const Vec3& checkpoint) const {
    player.position = checkpoint;
    player.prev_position = checkpoint;
    player.velocity = {};
    player.grounded = false;
    player.air_dash_available = true;
    player.dash_time_remaining = 0.0f;
    player.dash_cooldown_remaining = 0.0f;
    player.dash_reset_cooldown_remaining = 0.0f;
    player.no_dash_jump_remaining = 0.0f;
    player.movement_state = PlayerMovementState::Normal;
    player.wall_contact = false;
    player.hold_jump_time_remaining = 0.0f;
    player.auto_jump = false;
    player.contact = {};
    player.platform_carry = {};
    player.climb = {};
}

bool RespawnSystem::Step(PlayerState& player, const Vec3& checkpoint) const {
    if (player.position.y >= config_.respawn_fall_height) {
        return false;
    }
    ResetState(player, checkpoint);
    return true;
}

bool RespawnSystem::Step(
    PlayerState& player,
    const Vec3& checkpoint,
    const Room& room,
    const MoveService& mover,
    bool death_triggered
) const {
    if (!death_triggered && player.position.y >= config_.respawn_fall_height) {
        return false;
    }
    ResetState(player, checkpoint);
    // Resolve the grounded contact via the same single mover as gameplay
    // (OG ground snap): if a floor is within 5 units below the spawn, ground
    // the player on it.
    player.contact.was_grounded = false;
    mover.GroundSnap(player, room, 1.0f / 60.0f);
    player.grounded = mover.GroundCheck(player, room, nullptr, nullptr);
    player.contact.was_grounded = player.grounded;
    return true;
}

}  // namespace madeline_cube
