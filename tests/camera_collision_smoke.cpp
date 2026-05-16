#include <cassert>
#include <cmath>

#include "../src/user/gameplay/player/camera_controller.hpp"
#include "../src/user/gameplay/world/world.hpp"

using namespace madeline_cube;

int main() {
    // --- Boom shortens when an obstruction sits between look-at and the
    //     desired camera position. The camera uses the source-shaped
    //     RaycastRoomSource (with backface-ignore) for this query, so we
    //     also assert the same source query reports the same obstruction. ---
    {
        Room room;
        room.colliders[room.collider_count++] = {
            .type = ColliderType::Box,
            .bounds = {.min = {-1.0f, 0.0f, -4.0f}, .max = {1.0f, 4.0f, -3.0f}},
            .solid = true,
        };

        CameraController controller;
        CameraState camera;
        CameraState unobstructed;
        camera.target_forward = {0.0f, 0.0f, 1.0f};
        unobstructed.target_forward = camera.target_forward;
        controller.Reset(camera, {0.0f, 0.0f, 0.0f});
        controller.Reset(unobstructed, {0.0f, 0.0f, 0.0f});
        controller.Step(camera, {0.0f, 0.0f, 0.0f}, false, {}, 1.0f / 60.0f, &room);
        controller.Step(unobstructed, {0.0f, 0.0f, 0.0f}, false, {}, 1.0f / 60.0f, nullptr);
        // Obstructed boom must end up closer than the free boom.
        assert(camera.position.z > unobstructed.position.z);

        // The camera must share the source-shaped query layer: a probe from
        // the look-at toward the unobstructed desired position must hit the
        // same box.
        const Vec3 look_at = {0.0f, 1.2f, 0.0f};  // matches DesiredLookAt
        const Vec3 dir = {0.0f, 0.0f, -1.0f};      // boom points along -Z
        const GroundHit hit = RaycastRoomSource(room, look_at, dir, 10.0f, BackfacePolicy::Ignore);
        assert(hit.hit);
        assert(hit.normal.z > 0.5f);  // box face normal points back toward +Z
    }

    // --- Floor query is source-shaped: confirms the room query layer answers
    //     the floor probe the camera path relies on internally. ---
    {
        Room room;
        room.colliders[room.collider_count++] = {
            .type = ColliderType::Plane,
            .bounds = {.min = {-10.0f, 0.0f, -10.0f}, .max = {10.0f, 0.0f, 10.0f}},
            .solid = true,
            .normal = {0.0f, 1.0f, 0.0f},
        };
        const GroundHit floor = QueryFloorSource(room, {0.0f, 5.0f, 0.0f}, 10.0f);
        assert(floor.hit);
        assert(std::fabs(floor.point.y - 0.0f) < 0.001f);
    }

    return 0;
}
