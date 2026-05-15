#pragma once

#include "bobbing_actor.hpp"

namespace madeline_cube {

class StrawberryActor : public BobbingActor {
public:
    void Init() override;
    void Update(float delta_seconds) override;
    void OnCollect() override;
    bool IsCollectible() const override { return true; }
};

}  // namespace madeline_cube
