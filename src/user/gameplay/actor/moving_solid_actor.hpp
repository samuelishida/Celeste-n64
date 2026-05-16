#pragma once
#include "gameplay/actor/solid_actor.hpp"
#include "gameplay/actor/traits.hpp"
namespace madeline_cube {
class MovingSolidActor : public SolidActor, public RidePlatformTrait {
public:
    Vec3 target_position;
    Vec3 last_position;
    Vec3 displacement;
    Vec3 rider_velocity;
    float carry_storage_time = 0.1f;

    void Advance(float delta_seconds);
};
}
