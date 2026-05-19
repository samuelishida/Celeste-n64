#include "gameplay/actor/strawberry_actor.hpp"

#include <cmath>

namespace madeline_cube {

void StrawberryActor::Init() {
    InitBobbing();
    placeholder_id = 2;
    pickup_radius = 15.0f;
    collected = false;
    active = false;
}

void StrawberryActor::Update(float delta_seconds) {
    if (collected && !active) {
        return;
    }

    UpdateBobbing(delta_seconds);
}

void StrawberryActor::OnCollect() {
    if (collected) return;
    collected = true;
    active = false;
}

}  // namespace madeline_cube
