#include "spring_actor.hpp"

namespace madeline_cube {

void SpringActor::Init() {
    placeholder_id = 7;  // actor_spring
    pickup_radius = 1.5f;
}

void SpringActor::OnCollect() {
    // Spring doesn't get "collected" in the traditional sense;
    // it launches the player. The gameplay scene handles this.
}

}  // namespace madeline_cube
