#pragma once

#include <cstdint>

#include "gameplay/math_types.hpp"

namespace madeline_cube {

class DynamicSolidGeometry;
class PlayerController;
struct PlayerState;
struct Room;

struct DynamicSolidMoveResult {
    bool found = false;
    bool moved = false;
    bool carried_player = false;
};

// Source-shaped Solid.MoveTo for actor-owned transformed-face collision.
// Carry happens against the pre-move pose while this solid is non-collidable;
// transform/reinsert happens only after the rider sweep completes.
DynamicSolidMoveResult MoveDynamicSolidTo(
    DynamicSolidGeometry& geometry, uint32_t source_id,
    const Vec3& target_position, const Vec3& source_velocity,
    PlayerController& player_controller, PlayerState& player,
    const Room& room);

}  // namespace madeline_cube
