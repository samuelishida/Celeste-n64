#pragma once

#include "gameplay/actor/bobbing_actor.hpp"

namespace madeline_cube {

class StrawberryActor : public BobbingActor {
public:
    static constexpr uint16_t kTypeId = static_cast<uint16_t>(ActorTypeId::kStrawberry);
    StrawberryActor() { type_id_ = kTypeId; }

    void Init() override;
    void Update(float delta_seconds) override;
    void OnCollect() override;

    bool IsCollectible() const override { return true; }
};

}  // namespace madeline_cube