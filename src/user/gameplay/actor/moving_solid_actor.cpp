#include "gameplay/actor/moving_solid_actor.hpp"
namespace madeline_cube {
void MovingSolidActor::Advance(float dt) {
    displacement = {
        target_position.x - last_position.x,
        target_position.y - last_position.y,
        target_position.z - last_position.z,
    };
    position = target_position;
    rider_velocity = dt > 0.0f
        ? Vec3{displacement.x / dt, displacement.y / dt, displacement.z / dt}
        : Vec3{};
    for (int i = 0; i < face_count; ++i) {
        faces[i].bounds.min.x += displacement.x; faces[i].bounds.max.x += displacement.x;
        faces[i].bounds.min.y += displacement.y; faces[i].bounds.max.y += displacement.y;
        faces[i].bounds.min.z += displacement.z; faces[i].bounds.max.z += displacement.z;
        faces[i].velocity = rider_velocity;
    }
    last_position = target_position;
}
}
