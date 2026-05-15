#pragma once

#include "actor.hpp"

namespace madeline_cube {

class SpringActor : public Actor {
public:
    void Init() override;
    bool IsCollectible() const override { return true; }

    float launch_speed_y = 16.0f;
};

}  // namespace madeline_cube
