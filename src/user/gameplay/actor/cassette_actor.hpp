#pragma once

#include "gameplay/actor/actor.hpp"

namespace madeline_cube {

class CassetteActor : public Actor {
public:
    static constexpr uint16_t kTypeId = static_cast<uint16_t>(ActorTypeId::kCassette);
    CassetteActor() { type_id_ = kTypeId; }

    void InitAt(const Vec3& start_position);
    bool Step(float delta_seconds, const Vec3& player_position);

    float SpinYawRadians() const { return spin_phase_seconds_ * 1.5f; }

    // Target level to load when cassette is collected (empty = none).
    const char* target_level_path = nullptr;

private:
    float spin_phase_seconds_ = 0.0f;
};

}  // namespace madeline_cube
