#pragma once

#include "gameplay/actor/actor.hpp"

namespace madeline_cube {

class SpringActor : public Actor {
public:
    static constexpr uint16_t kTypeId = static_cast<uint16_t>(ActorTypeId::kSpring);
    SpringActor() { type_id_ = kTypeId; }

    void Init() override;
    bool IsCollectible() const override { return true; }

    float launch_speed_y = 160.0f;
};

}  // namespace madeline_cube
