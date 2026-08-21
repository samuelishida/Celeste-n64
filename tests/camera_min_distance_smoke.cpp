#include <cassert>
#include <cmath>
#include <cstdio>

#include "../src/user/gameplay/player/camera_controller.hpp"
#include "../src/user/gameplay/world/world.hpp"

using namespace madeline_cube;

// Inc 6 regression: the camera's minimum distance from the player must hold
// even when wall collision pushes the camera into the player's extent.
//
// Setup: player at origin, grounded, max zoom-in (target_distance = 0.0).
//   - look_at = (0, 12, 0)  (look_at_height = 12)
//   - desired camera = (0, 13, -30)  (distance 30, height 1 at max zoom)
//   - a wall box sits between the look-at and the desired camera, so the boom
//     ray from look_at toward the desired position hits the box's +Z face and
//     pushes the camera in to ~ (0, 12.3, -8.5)  =>  ~14.95 units from the
//     player. That is INSIDE the near plane (5.0) for the cube's far corner,
//     which is exactly the z-split clip the fix targets.
//
// Without the kCameraMinDistance clamp the camera converges to ~14.95 units
// (assert below fails). With the clamp it converges to exactly 18.0 units
// (assert passes). The wall box is placed so the ceiling probe (5u up from the
// pushed camera at z ~ -8.5) stays in front of the box (z in [-11,-9]) and does
// not interfere.
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
    // Reset() forces the cold-start target_distance = 0.5 (high camera). We
    // want max zoom-in (target_distance = 0.0 => low camera, height 1) so the
    // wall push brings the camera inside the minimum distance and the clamp
    // fires. Set it AFTER Reset so it is not overwritten.
    camera.target_distance = 0.0f;

    // Sanity: confirm the wall actually obstructs the boom (the camera must be
    // pushed in well short of the free -30 position).
    for (int i = 0; i < kFrames; ++i) {
        controller.Step(camera, {0.0f, 0.0f, 0.0f},
                        /*climbing=*/false, /*grounded=*/true,
                        /*horizontal_speed=*/0.0f,
                        /*input=*/{}, kDt, &room);
    }

    // The wall must have pushed the camera in (z far closer than the free -30).
    assert(camera.position.z > -20.0f);

    // The camera-to-player distance must respect the minimum-distance clamp.
    const float dx = camera.position.x - 0.0f;
    const float dy = camera.position.y - 0.0f;
    const float dz = camera.position.z - 0.0f;
    const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
    std::printf("camera pos = (%.2f, %.2f, %.2f), dist = %.3f\n",
                camera.position.x, camera.position.y, camera.position.z, dist);
    assert(dist >= kMinDistance - kTolerance);

    // Control: with no room the camera is never pushed in, so it sits well
    // beyond the minimum distance and the clamp is a no-op. This guards against
    // the clamp over-firing in open space.
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
    }

    std::printf("camera_min_distance_smoke: OK\n");
    return 0;
}
