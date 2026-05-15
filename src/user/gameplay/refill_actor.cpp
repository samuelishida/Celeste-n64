#include "refill_actor.hpp"

#include <cmath>

namespace madeline_cube {

void RefillActor::Init() {
    InitBobbing();
    placeholder_id = 3;  // pickup_refill
    pickup_radius = 1.5f;
}

void RefillActor::Update(float delta_seconds) {
    if (collected && !active) {
        respawn_timer -= delta_seconds;
        if (respawn_timer <= 0.0f) {
            active = true;
            collected = false;
        }
        return;
    }

    UpdateBobbing(delta_seconds);
}

void RefillActor::OnCollect() {
    if (collected) return;
    active = false;
    collected = true;
    respawn_timer = kRespawnTime;
}

}  // namespace madeline_cube
