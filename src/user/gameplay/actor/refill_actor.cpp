#include "gameplay/actor/refill_actor.hpp"

#include <cmath>

namespace madeline_cube {

void RefillActor::Init() {
    InitBobbing();
    bob_time_ = 0.0f;
    pickup_radius = kPickupRadius;
    cooldown_remaining_ = 0.0f;
}

void RefillActor::Update(float delta_seconds) {
    if (!std::isfinite(delta_seconds) || delta_seconds <= 0.0f) return;
    bob_time_ += delta_seconds;

    if (collected && !active) {
        cooldown_remaining_ -= delta_seconds;
        if (cooldown_remaining_ <= 0.0f) {
            cooldown_remaining_ = 0.0f;
            active = true;
            collected = false;
            update_off_screen = false;
        }
    }
}

void RefillActor::OnCollect() {
    if (!active || collected) return;
    collected = true;
    active = false;
    cooldown_remaining_ = kCooldownSeconds;
    update_off_screen = true;
}

bool RefillActor::CanPickup(int dashes) const {
    return active && !collected && cooldown_remaining_ <= 0.0f &&
           dashes < DashAmount();
}

bool RefillActor::OverlapsPickupPoint(const Vec3& point) const {
    const float dx = point.x - position.x;
    const float dy = point.y - position.y;
    const float dz = point.z - position.z;
    return dx * dx + dy * dy + dz * dz < pickup_radius * pickup_radius;
}

Vec3 RefillActor::VisualPosition() const {
    Vec3 visual = base_position_;
    visual.y += std::sin(bob_time_ * kVisualBobRate) * kVisualBobAmplitude;
    return visual;
}

}  // namespace madeline_cube
