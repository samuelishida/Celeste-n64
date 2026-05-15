#pragma once

#include "actor.hpp"

namespace madeline_cube {

class BobbingActor : public Actor {
public:
    void InitBobbing();
    void UpdateBobbing(float delta_seconds);

protected:
    float bob_time_ = 0.0f;
    Vec3 base_position_;
};

}  // namespace madeline_cube
