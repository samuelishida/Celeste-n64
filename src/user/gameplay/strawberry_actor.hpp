#pragma once

#include "actor.hpp"

namespace madeline_cube {

class StrawberryActor : public Actor {
public:
    void Init() override;
    void Update(float delta_seconds) override;
    void OnCollect() override;
    bool IsCollectible() const override { return true; }

private:
    float bob_time_ = 0.0f;
    Vec3 base_position_;
};

}  // namespace madeline_cube
