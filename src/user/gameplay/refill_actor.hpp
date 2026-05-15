#pragma once

#include "actor.hpp"

namespace madeline_cube {

class RefillActor : public Actor {
public:
    void Init() override;
    void Update(float delta_seconds) override;
    void OnCollect() override;
    bool IsCollectible() const override { return true; }

    bool used = false;
    float respawn_timer = 0.0f;
    static constexpr float kRespawnTime = 3.0f;

private:
    float bob_time_ = 0.0f;
    Vec3 base_position_;
};

}  // namespace madeline_cube
