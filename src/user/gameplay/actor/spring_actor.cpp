#include "gameplay/actor/spring_actor.hpp"

namespace madeline_cube {

void SpringActor::Init() {
    placeholder_id = 7;  // actor_spring
    update_off_screen = false;
    // OG Spring.cs PickupRadius = 16 (the launch itself lives in
    // PlayerController::Spring, wired by GameplayScene on player overlap).
    pickup_radius = 16.0f;
    cooldown_remaining = 0.0f;
}

void SpringActor::Update(float delta_seconds) {
    if (cooldown_remaining > 0.0f) {
        cooldown_remaining -= delta_seconds;
        if (cooldown_remaining <= 0.0f) {
            cooldown_remaining = 0.0f;
            update_off_screen = false;
        }
    }
}

}  // namespace madeline_cube
