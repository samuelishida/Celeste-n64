#pragma once

#include "gameplay/actor/actor.hpp"

namespace madeline_cube {

class CassetteActor : public Actor {
public:
    static constexpr uint16_t kTypeId = static_cast<uint16_t>(ActorTypeId::kCassette);
    static constexpr float kPickupRadius = 2.0f;  // OG 10-unit radius at map scale 0.2
    CassetteActor() { type_id_ = kTypeId; }

    void InitAt(const Vec3& start_position);
    void Update(float delta_seconds) override;
    Vec3 UpdateBoundsHalfExtent() const override { return {8.0f, 8.0f, 8.0f}; }
    bool TryPickup(const Vec3& player_position);
    // Compatibility wrapper for legacy single-room callers.
    bool Step(float delta_seconds, const Vec3& player_position);

    float SpinYawRadians() const { return spin_phase_seconds_ * 3.0f; }

    // Target level to load when cassette is collected (empty = none).
    const char* target_level_path = nullptr;

private:
    float spin_phase_seconds_ = 0.0f;
};

}  // namespace madeline_cube
