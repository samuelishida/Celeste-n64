#pragma once

#include "gameplay/actor/actor.hpp"
#include <cmath>

namespace madeline_cube {

class BobbingActor : public Actor {
public:
    ~BobbingActor() override = default;

    void Init() override { InitBobbing(); }
    void Update(float delta_seconds) override { UpdateBobbing(delta_seconds); }

    void InitBobbing() { base_position_ = position; }
    void UpdateBobbing(float delta_seconds) {
        bob_time_ += delta_seconds;
        position.y = base_position_.y + std::sin(bob_time_ * 3.0f) * 3.0f;
    }

protected:
    float bob_time_ = 0.f;
    Vec3 base_position_;
};

}  // namespace madeline_cube