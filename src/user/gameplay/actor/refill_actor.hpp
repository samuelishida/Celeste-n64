#pragma once

#include "gameplay/actor/bobbing_actor.hpp"

namespace madeline_cube {

class RefillActor : public BobbingActor {
public:
    static constexpr uint16_t kTypeId = static_cast<uint16_t>(ActorTypeId::kRefill);
    RefillActor() { type_id_ = kTypeId; }
    ~RefillActor() override = default;
    void Init() override;
    void Update(float delta_seconds) override;
    void OnCollect() override;

    bool IsCollectible() const override { return true; }

private:
    float respawn_time_ = 3.0f;
};

}  // namespace madeline_cube