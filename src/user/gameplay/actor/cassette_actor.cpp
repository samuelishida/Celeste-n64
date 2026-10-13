#include "gameplay/actor/cassette_actor.hpp"

namespace madeline_cube {

void CassetteActor::InitAt(const Vec3& start_position) {
    position = start_position;
    pickup_radius = kPickupRadius;
    collected = false;
    active = true;
    spin_phase_seconds_ = 0.0f;
}

void CassetteActor::Update(float delta_seconds) {
    spin_phase_seconds_ += delta_seconds;
}

bool CassetteActor::TryPickup(const Vec3& player_position) {
    if (collected) return false;
    const float dx = position.x - player_position.x;
    const float dy = position.y - player_position.y;
    const float dz = position.z - player_position.z;
    const float distance_squared = (dx * dx) + (dy * dy) + (dz * dz);
    const float radius_squared = pickup_radius * pickup_radius;
    if (distance_squared > radius_squared) {
        return false;
    }

    collected = true;
    return true;
}

bool CassetteActor::Step(float delta_seconds, const Vec3& player_position) {
    Update(delta_seconds);
    return TryPickup(player_position);
}

}  // namespace madeline_cube
