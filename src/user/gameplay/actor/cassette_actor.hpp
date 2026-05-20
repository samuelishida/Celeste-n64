#pragma once

#include "gameplay/actor/actor.hpp"

namespace madeline_cube {

class CassetteActor : public Actor {
public:
    void InitAt(const Vec3& start_position);
    bool Step(float delta_seconds, const Vec3& player_position);

    float SpinYawRadians() const { return spin_phase_seconds_ * 1.5f; }

private:
    float spin_phase_seconds_ = 0.0f;
};

}  // namespace madeline_cube
