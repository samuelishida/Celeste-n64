#include <cassert>
#include <cmath>

#include "../src/user/gameplay/player/movement_config.hpp"
#include "../src/user/gameplay/player/player_motor.hpp"
#include "../src/user/gameplay/player/player_state.hpp"
#include "../src/user/gameplay/world/respawn_system.hpp"
#include "../src/user/gameplay/world/world.hpp"

using namespace madeline_cube;

namespace {

bool ApproxEq(float a, float b, float eps = 0.0005f) {
    return std::fabs(a - b) <= eps;
}

Room BuildRespawnRoom() {
    Room room;
    room.colliders[room.collider_count++] = {
        .type = ColliderType::Plane,
        .bounds = {.min = {-10.0f, 0.0f, -10.0f}, .max = {10.0f, 0.0f, 10.0f}},
        .solid = true,
        .normal = {0.0f, 1.0f, 0.0f},
    };
    return room;
}

}  // namespace

int main() {
    MovementConfig config;
    RespawnSystem respawn(config);
    PlayerMotor motor;
    const Room room = BuildRespawnRoom();

    // --- Respawn directly on the floor: motor.RefreshContacts resolves the
    //     grounded state instead of the respawn writing it directly. ---
    {
        PlayerState player;
        player.position.y = config.respawn_fall_height - 1.0f;
        player.velocity = {3.0f, -50.0f, -2.0f};
        player.grounded = true;
        player.movement_state = PlayerMovementState::Dashing;
        player.dash_time_remaining = 0.5f;
        player.dash_reset_cooldown_remaining = 0.5f;
        player.air_dash_available = false;
        player.platform_carry.stored_velocity = {10.0f, 0.0f, 0.0f};
        player.platform_carry.time_remaining = 0.25f;

        // Spawn point sits exactly on the floor: motor.RefreshContacts
        // should resolve grounded=true via QueryFloorSource.
        const Vec3 checkpoint = {0.0f, 1.0f, 0.0f};
        const bool did = respawn.Step(player, checkpoint, room, motor);
        assert(did);
        assert(ApproxEq(player.position.x, checkpoint.x));
        assert(ApproxEq(player.position.y, checkpoint.y));
        assert(ApproxEq(player.position.z, checkpoint.z));
        assert(ApproxEq(player.velocity.x, 0.0f));
        assert(ApproxEq(player.velocity.y, 0.0f));
        assert(ApproxEq(player.velocity.z, 0.0f));
        assert(player.grounded);  // motor resolved this, not the respawn
        assert(player.movement_state == PlayerMovementState::Normal);
        assert(player.air_dash_available);
        assert(ApproxEq(player.dash_time_remaining, 0.0f));
        assert(ApproxEq(player.dash_reset_cooldown_remaining, 0.0f));
        assert(ApproxEq(player.platform_carry.time_remaining, 0.0f));
    }

    // --- Respawn above the floor: motor.RefreshContacts must leave the
    //     player ungrounded (no ground snap, since the player was not
    //     previously grounded after the reset). ---
    {
        PlayerState player;
        player.position.y = config.respawn_fall_height - 1.0f;
        const Vec3 checkpoint = {0.0f, 5.0f, 0.0f};  // 4 units above floor
        const bool did = respawn.Step(player, checkpoint, room, motor);
        assert(did);
        assert(ApproxEq(player.position.y, checkpoint.y));
        assert(!player.grounded);
    }

    // --- Respawn above kill plane: no-op, returns false. ---
    {
        PlayerState player;
        player.position.y = config.respawn_fall_height + 1.0f;
        const Vec3 original = player.position;
        const bool did = respawn.Step(player, {0.0f, 0.0f, 0.0f}, room, motor);
        assert(!did);
        assert(player.position.y == original.y);
    }

    // --- Two-arg overload still works for host smoke callers that have no
    //     room (e.g. gameplay_smoke kill-plane case). ---
    {
        PlayerState player;
        player.position.y = config.respawn_fall_height - 1.0f;
        const bool did = respawn.Step(player, {0.0f, 2.0f, 0.0f});
        assert(did);
        assert(ApproxEq(player.position.y, 2.0f));
        assert(!player.grounded);
        assert(player.air_dash_available);
    }

    return 0;
}
