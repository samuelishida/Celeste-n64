#pragma once

#include "bobbing_actor.hpp"

namespace madeline_cube {

class RefillActor : public BobbingActor {
public:
    void Init() override;
    void Update(float delta_seconds) override;
    void OnCollect() override;
    bool IsCollectible() const override { return true; }

    float respawn_timer = 0.0f;
    static constexpr float kRespawnTime = 3.0f;
};

}  // namespace madeline_cube
