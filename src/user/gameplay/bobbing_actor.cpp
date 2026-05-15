#include "bobbing_actor.hpp"

#include <cmath>

namespace madeline_cube {

void BobbingActor::InitBobbing() {
    base_position_ = position;
}

void BobbingActor::UpdateBobbing(float delta_seconds) {
    bob_time_ += delta_seconds;
    position.y = base_position_.y + std::sin(bob_time_ * 3.0f) * 0.3f;
}

}  // namespace madeline_cube
