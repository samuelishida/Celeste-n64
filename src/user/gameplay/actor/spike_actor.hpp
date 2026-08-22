#pragma once
#include "gameplay/actor/actor.hpp"
#include "gameplay/actor/traits.hpp"
namespace madeline_cube {
class SpikeActor : public Actor, public HazardTrait {
public:
    static constexpr uint16_t kTypeId = static_cast<uint16_t>(ActorTypeId::kSpike);
    SpikeActor() { type_id_ = kTypeId; }
};
}
