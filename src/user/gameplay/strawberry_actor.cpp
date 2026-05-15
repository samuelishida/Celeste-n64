#include "strawberry_actor.hpp"

#include <cmath>

namespace madeline_cube {

void StrawberryActor::Init() {
    InitBobbing();
    placeholder_id = 2;  // pickup_strawberry
    pickup_radius = 1.5f;
}

void StrawberryActor::Update(float delta_seconds) {
    if (collected) {
        return;
    }

    UpdateBobbing(delta_seconds);
}

void StrawberryActor::OnCollect() {
    collected = true;
    active = false;
}

}  // namespace madeline_cube
