#include "gameplay/world/respawn_system.hpp"

namespace madeline_cube {

RespawnSystem::RespawnSystem(MovementConfig config) : config_(config) {}

void RespawnSystem::ResetState(PlayerState& player, const Vec3& checkpoint) const {
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
    player.contact = {};
    player.platform_carry = {};
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
    const PlayerMotor& motor
) const {
    if (player.position.y >= config_.respawn_fall_height) {
        return false;
    }
    ResetState(player, checkpoint);
    MotorInput refresh_input;
    refresh_input.requested_velocity = {0.0f, 0.0f, 0.0f};
    refresh_input.wants_ground_snap = false;
    refresh_input.wants_coyote_refresh = true;
    refresh_input.wants_dash_refill = true;
    motor.RefreshContacts(player, room, refresh_input);
    return true;
}

}  // namespace madeline_cube
