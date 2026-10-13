#include "gameplay/world/dynamic_solid_world.hpp"

#include <cmath>

#include "gameplay/player/player_controller.hpp"
#include "gameplay/player/player_state.hpp"
#include "gameplay/world/dynamic_solid_geometry.hpp"
#include "gameplay/world/world.hpp"

namespace madeline_cube {
namespace {

bool Finite(const Vec3& v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

class RestoreCollision {
public:
    explicit RestoreCollision(DynamicSolidView& view)
        : view_(view), was_collidable_(view.collidable) {
        view_.collidable = false;
    }
    ~RestoreCollision() { view_.collidable = was_collidable_; }

    RestoreCollision(const RestoreCollision&) = delete;
    RestoreCollision& operator=(const RestoreCollision&) = delete;

private:
    DynamicSolidView& view_;
    bool was_collidable_;
};

bool PlayerRides(int owner_id, const PlayerState& player,
                 const PlayerController& controller, const Room& room) {
    if (player.movement_state == PlayerMovementState::Climbing &&
        player.climb.wall_surface_id == owner_id) return true;

    GroundHit support{};
    return controller.Mover().GroundCheck(player, room, nullptr, nullptr,
                                          &support) &&
           support.owner_id == owner_id;
}

}  // namespace

DynamicSolidMoveResult MoveDynamicSolidTo(
    DynamicSolidGeometry& geometry, uint32_t source_id,
    const Vec3& target_position, const Vec3& source_velocity,
    PlayerController& player_controller, PlayerState& player,
    const Room& room) {
    DynamicSolidMoveResult result{};
    if (room.dynamic_solids != &geometry || !Finite(target_position) ||
        !Finite(source_velocity)) return result;

    DynamicSolidView* views = geometry.MutableViews();
    uint16_t actor_index = 0;
    while (actor_index < geometry.ActorCount() &&
           views[actor_index].source_id != source_id) ++actor_index;
    if (actor_index >= geometry.ActorCount()) return result;
    result.found = true;

    DynamicSolidView& view = views[actor_index];
    view.velocity = source_velocity;
    const Vec3 displacement{
        target_position.x - view.position.x,
        target_position.y - view.position.y,
        target_position.z - view.position.z};
    if (displacement.x == 0.0f && displacement.y == 0.0f &&
        displacement.z == 0.0f) return result;

    const int owner_id = 0x40000000 | actor_index;
    if (view.collidable && PlayerRides(owner_id, player,
                                       player_controller, room)) {
        {
            RestoreCollision restore(view);
            player_controller.RidingPlatformSetVelocity(player,
                                                         source_velocity);
            player_controller.RidingPlatformMoved(player, room,
                                                   displacement);
        }
        result.carried_player = true;
    }

    result.moved = geometry.SetTransform(source_id, target_position,
                                         view.yaw_radians);
    return result;
}

}  // namespace madeline_cube
