#pragma once

#include "gameplay/actor/actor.hpp"

namespace madeline_cube {

class SpringActor : public Actor {
public:
    static constexpr uint16_t kTypeId = static_cast<uint16_t>(ActorTypeId::kSpring);
    // OG Spring.cs PickupRadius = 16 (and the player overlap gate below).
    static constexpr float kCooldown = 1.0f;  // OG Spring.cs tCooldown = 1.0f

    SpringActor() { type_id_ = kTypeId; }

    void Init() override;
    void Update(float delta_seconds) override;
    bool IsCollectible() const override { return true; }

    // OG Spring.cs arm/decay: after a launch the board ignores the player for
    // kCooldown seconds, so a standing player isn't relaunched every tick.
    float cooldown_remaining = 0.0f;
};

}  // namespace madeline_cube
