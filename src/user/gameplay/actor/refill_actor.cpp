#include "gameplay/actor/refill_actor.hpp"

#include <cmath>

namespace madeline_cube {

void RefillActor::Init() {
    InitBobbing();
    pickup_radius = 1.5f;
    respawn_time_ = 3.0f;
}

void RefillActor::Update(float delta_seconds) {
    if (collected && !active) {
        respawn_time_ -= delta_seconds;
        if (respawn_time_ <= 0.0f) {
            active = true;
            collected = false;
        }
        return;
    }

    UpdateBobbing(delta_seconds);
}

void RefillActor::OnCollect() {
    if (collected) return;
    collected = true;
    active = false;
    respawn_time_ = 3.0f;
}

}  // namespace madeline_cube
