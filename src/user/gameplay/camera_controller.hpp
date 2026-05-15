#pragma once

#include "math_types.hpp"

namespace madeline_cube {

struct CameraState {
    Vec3 position = {0.0f, 6.0f, -10.0f};
    Vec3 target = {0.0f, 0.0f, 0.0f};
};

struct CameraConfig {
    Vec3 follow_offset = {0.0f, 6.0f, -10.0f};
    float follow_lerp = 8.0f;
};

class CameraController {
public:
    explicit CameraController(CameraConfig config = {});

    void Step(CameraState& camera, const Vec3& player_position, float delta_seconds) const;

private:
    CameraConfig config_;
};

}  // namespace madeline_cube

