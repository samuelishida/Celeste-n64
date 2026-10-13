#pragma once

#include "gameplay/actor/bobbing_actor.hpp"

namespace madeline_cube {

class RefillActor : public BobbingActor {
public:
    static constexpr uint16_t kTypeId = static_cast<uint16_t>(ActorTypeId::kRefill);
    static constexpr float kPickupRadius = 20.0f;
    static constexpr float kCooldownSeconds = 4.0f;
    static constexpr float kVisualBobAmplitude = 2.0f;
    static constexpr float kVisualBobRate = 2.0f;
    static constexpr float kVisualSpinRate = 3.0f;
    RefillActor() { type_id_ = kTypeId; }
    ~RefillActor() override = default;
    void Init() override;
    void Update(float delta_seconds) override;
    void OnCollect() override;
    bool UpdatesWhenInactive() const override { return true; }
    Vec3 UpdateBoundsHalfExtent() const override { return {3.0f, 3.0f, 3.0f}; }

    void SetDouble(bool value) { is_double_ = value; }
    bool IsDouble() const { return is_double_; }
    int DashAmount() const { return is_double_ ? 2 : 1; }
    bool CanPickup(int dashes) const;
    bool OverlapsPickupPoint(const Vec3& point) const;
    float CooldownRemaining() const { return cooldown_remaining_; }
    Vec3 VisualPosition() const;
    float VisualSpinYawRadians() const { return bob_time_ * kVisualSpinRate; }

    bool IsCollectible() const override { return true; }

private:
    float cooldown_remaining_ = 0.0f;
    bool is_double_ = false;
};

}  // namespace madeline_cube
