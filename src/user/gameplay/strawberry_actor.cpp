#include "strawberry_actor.hpp"

#include <cmath>

namespace madeline_cube {

void StrawberryActor::Init() {
    base_position_ = position;
    placeholder_id = 2;  // pickup_strawberry
    pickup_radius = 1.5f;
}

void StrawberryActor::Update(float delta_seconds) {
    if (collected) {
        active = false;
        return;
    }

    bob_time_ += delta_seconds;
    position.y = base_position_.y + std::sin(bob_time_ * 3.0f) * 0.3f;
}

void StrawberryActor::OnCollect() {
    collected = true;
    active = false;
}

}  // namespace madeline_cube
