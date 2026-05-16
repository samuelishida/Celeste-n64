#pragma once

#include "gameplay/math_types.hpp"
#include "gameplay/world/world.hpp"

namespace madeline_cube {

struct CameraState {
    Vec3 position = {0.0f, 3.0f, -6.0f};
    Vec3 target = {0.0f, 0.0f, 0.0f};
    Vec3 origin = {0.0f, 0.0f, 0.0f};
    Vec3 target_forward = {0.0f, 0.0f, 1.0f};
    float target_distance = 0.5f;
    bool initialized = false;
};

struct CameraInput {
    float orbit = 0.0f;
    float zoom = 0.0f;
};

struct CameraConfig {
    float rotation_speed = 4.0f;
    float vertical_pad = 0.8f;
    float look_at_height = 1.2f;
    float zoom_snap_interval = 1.0f / 3.0f;
    float zoom_snap_threshold = 0.1f;
    float vertical_follow_decay = 0.001f;
    float camera_follow_decay = 0.01f;
};

class CameraController {
public:
    explicit CameraController(CameraConfig config = {});

    void Reset(CameraState& camera, const Vec3& player_position) const;
    void Step(
        CameraState& camera,
        const Vec3& player_position,
        bool climbing,
        const CameraInput& input,
        float delta_seconds,
        const Room* room = nullptr
    ) const;

private:
    CameraConfig config_;
};

}  // namespace madeline_cube
