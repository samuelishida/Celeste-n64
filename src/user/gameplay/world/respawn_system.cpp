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
    // Resolve the grounded contact via the same single mover as gameplay.
    // ResetState clears was_grounded, and GroundSnap requires it — so the
    // snap call is only useful when the respawn path WANTS a snap; the real
    // contact authority is the GroundCheck below (review S3: dead-call note).
    player.contact.was_grounded = false;
    mover.GroundSnap(player, room, 1.0f / 60.0f);
    Vec3 respawn_normal{};
    player.grounded = mover.GroundCheck(player, room, nullptr, &respawn_normal);
    player.contact.was_grounded = player.grounded;
    if (player.grounded) {
        // S6: carry the actual floor normal (was {0,1,0} until first tick).
        player.contact.ground_normal = respawn_normal;
    }
    // Review S3: clear the timers the old ResetState never touched — a death
    // within the coyote window otherwise leaves a stale coyote_height that a
    // post-respawn jump would snap to. (Inc 1: the dash press buffer moved to
    // the input device; the scene consumes the device dash edge on respawn.)
    player.coyote_time_remaining = 0.0f;
    player.no_move_time_remaining = 0.0f;
    player.contact.ground_snap_cooldown_remaining = 0.0f;
    return true;
}

}  // namespace madeline_cube
