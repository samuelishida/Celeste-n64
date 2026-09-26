#include <cassert>
#include <cmath>
#include <cstdio>

#include "../src/user/gameplay/player/camera_controller.hpp"
#include "../src/user/gameplay/world/world.hpp"

using namespace madeline_cube;

// Camera obstruction regression: never enforce minimum distance by pushing the
// camera through a wall. Lower the render near plane while the shortened boom
// is close to the avatar so the wall does not hide the player mesh.
//
// Setup: player at origin, grounded, max zoom-in (target_distance = 0.0).
//   - look_at = (0, 12, 0)  (look_at_height = 12)
//   - desired camera = (0, 13, -30)  (distance 30, height 1 at max zoom)
//   - a wall box sits between the look-at and the desired camera, so the boom
//     ray from look_at toward the desired position hits the box's +Z face and
//     pushes the camera in to ~ (0, 12.3, -8.5) => ~14.95 units from the
//     player. This fixture reproduces a boom shortened by wall collision.
//
// The wall is between look-at and the desired camera. The camera must remain
// on the player's side of its near face (z > -9), even though that means its
// distance from the player is below the old 18-unit clamp.
int main() {
    constexpr float kMinDistance = 18.0f;
    constexpr float kTolerance = 0.5f;
    constexpr int kFrames = 300;
    constexpr float kDt = 1.0f / 60.0f;

    Room room;
    // Wall between the look-at (0,12,0) and the desired camera (0,13,-30).
    // +Z face at z = -9; the boom hits it at ~(0, 12.3, -9).
    room.colliders[room.collider_count++] = {
        .type = ColliderType::Box,
        .bounds = {.min = {-5.0f, 0.0f, -11.0f}, .max = {5.0f, 20.0f, -9.0f}},
        .solid = true,
    };

    CameraController controller;
    CameraState camera;
    controller.Reset(camera, {0.0f, 0.0f, 0.0f});
    // Reset() forces the cold-start target_distance = 0.5 (high camera). Use
    // max zoom-in (target_distance = 0.0 => low camera, height 1) to match the
    // wall-clipped framing from the reported capture.
    camera.target_distance = 0.0f;

    // Sanity: confirm the wall actually obstructs the boom (the camera must be
    // kept in front of its z=-9 face, not pulled through by follow smoothing).
    for (int i = 0; i < kFrames; ++i) {
        controller.Step(camera, {0.0f, 0.0f, 0.0f},
                        /*climbing=*/false, /*grounded=*/true,
                        /*horizontal_speed=*/0.0f,
                        /*input=*/{}, kDt, &room);
        assert(camera.position.z > -9.0f);
        assert(camera.render_near_plane < 5.0f);
    }

    // The wall must have pushed the camera in (z far closer than the free -30).
    assert(camera.position.z > -20.0f);

    // The camera must remain on the player side of the wall. The previous
    // min-distance correction put it inside this box at z ~= -10.2.
    const float dx = camera.position.x - 0.0f;
    const float dy = camera.position.y - 0.0f;
    const float dz = camera.position.z - 0.0f;
    const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
    std::printf("camera pos = (%.2f, %.2f, %.2f), dist = %.3f\n",
                camera.position.x, camera.position.y, camera.position.z, dist);
    const bool camera_inside_wall =
        camera.position.x > -5.0f && camera.position.x < 5.0f &&
        camera.position.y > 0.0f && camera.position.y < 20.0f &&
        camera.position.z > -11.0f && camera.position.z < -9.0f;
    assert(!camera_inside_wall);
    assert(camera.position.z > -9.0f);
    assert(dist < kMinDistance - kTolerance);
    assert(camera.render_near_plane < 5.0f);
    assert(camera.render_near_plane >= 0.25f);

    // Control: with no room the camera remains at its normal follow distance
    // and keeps the default near plane.
    {
        CameraController free_controller;
        CameraState free_camera;
        free_controller.Reset(free_camera, {0.0f, 0.0f, 0.0f});
        free_camera.target_distance = 0.0f;  // after Reset (see above)
        for (int i = 0; i < kFrames; ++i) {
            free_controller.Step(free_camera, {0.0f, 0.0f, 0.0f},
                                 false, true, 0.0f, {}, kDt, nullptr);
        }
        const float fdx = free_camera.position.x;
        const float fdy = free_camera.position.y;
        const float fdz = free_camera.position.z;
        const float fdist = std::sqrt(fdx * fdx + fdy * fdy + fdz * fdz);
        std::printf("free camera pos = (%.2f, %.2f, %.2f), dist = %.3f\n",
                    free_camera.position.x, free_camera.position.y,
                    free_camera.position.z, fdist);
        assert(fdist > kMinDistance + kTolerance);
        assert(std::fabs(free_camera.render_near_plane - 5.0f) < 0.01f);
    }

    std::printf("camera_obstruction_smoke: OK\n");
    return 0;
}
