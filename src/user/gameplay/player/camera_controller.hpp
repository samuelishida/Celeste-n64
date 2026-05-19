#pragma once

#include "gameplay/math_types.hpp"
#include "gameplay/world/world.hpp"

namespace madeline_cube {

struct CameraState {
    Vec3 position = {0.0f, 30.0f, -60.0f};
    Vec3 target = {0.0f, 0.0f, 0.0f};
    Vec3 origin = {0.0f, 0.0f, 0.0f};
    Vec3 target_forward = {0.0f, 0.0f, 1.0f};
    float target_distance = 0.5f;
    float fov_multiplier = 1.0f;
    bool initialized = false;
};

struct CameraInput {
    float orbit = 0.0f;
    float zoom = 0.0f;
};

struct CameraConfig {
    float rotation_speed = 4.0f;
    float vertical_pad = 8.0f;
    float look_at_height = 12.0f;
    float zoom_snap_interval = 1.0f / 3.0f;
    float zoom_snap_threshold = 0.1f;
    float vertical_follow_decay = 0.001f;
    float camera_follow_decay = 0.01f;
    float fov_base_deg = 45.0f;
    float fov_speed_min = 76.8f;    // max_speed * 1.2
    float fov_speed_max = 120.0f;
    float fov_max_multiplier = 1.2f;
    float near_plane = 5.0f;        // used for wall collision offset
};

class CameraController {
public:
    explicit CameraController(CameraConfig config = {});

    // Cold-start or full reset: snaps camera to player immediately.
    // On first cold start (camera.initialized == false), resets target_forward
    // to default (0,0,1). On respawn reset, preserves target_forward and
    // target_distance so the camera orbit/zoom persists across deaths
    // (matching OG static storedCameraForward/storedCameraDistance behavior).
    void Reset(CameraState& camera, const Vec3& player_position);

    void Step(
        CameraState& camera,
        const Vec3& player_position,
        bool climbing,
        bool grounded,
        float horizontal_speed,
        const CameraInput& input,
        float delta_seconds,
        const Room* room = nullptr
    );

private:
    CameraConfig config_;
    // Persisted across respawns (OG: static storedCameraForward/storedCameraDistance)
    Vec3 stored_forward_ = {0.0f, 0.0f, 1.0f};
    float stored_distance_ = 0.5f;
    bool has_stored_ = false;
};

}  // namespace madeline_cube