#include "camera_controller.hpp"

namespace madeline_cube {
namespace {

float Lerp(float current, float target, float alpha) {
    return current + ((target - current) * alpha);
}

}  // namespace

CameraController::CameraController(CameraConfig config) : config_(config) {}

void CameraController::Step(CameraState& camera, const Vec3& player_position, float delta_seconds) const {
    const float alpha = config_.follow_lerp * delta_seconds > 1.0f ? 1.0f : config_.follow_lerp * delta_seconds;
    const Vec3 desired_position = {
        player_position.x + config_.follow_offset.x,
        player_position.y + config_.follow_offset.y,
        player_position.z + config_.follow_offset.z,
    };

    camera.position.x = Lerp(camera.position.x, desired_position.x, alpha);
    camera.position.y = Lerp(camera.position.y, desired_position.y, alpha);
    camera.position.z = Lerp(camera.position.z, desired_position.z, alpha);
    camera.target = player_position;
}

}  // namespace madeline_cube

