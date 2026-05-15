#include "refill_actor.hpp"

#include <cmath>

namespace madeline_cube {

void RefillActor::Init() {
    base_position_ = position;
    placeholder_id = 3;  // pickup_refill
    pickup_radius = 1.5f;
}

void RefillActor::Update(float delta_seconds) {
    if (used && !active) {
        respawn_timer -= delta_seconds;
        if (respawn_timer <= 0.0f) {
            used = false;
            active = true;
            collected = false;
        }
        return;
    }

    bob_time_ += delta_seconds;
    position.y = base_position_.y + std::sin(bob_time_ * 3.0f) * 0.3f;
}

void RefillActor::OnCollect() {
    if (used) return;
    used = true;
    active = false;
    collected = true;
    respawn_timer = kRespawnTime;
}

}  // namespace madeline_cube
