#include "camera_controller.hpp"

#include <cmath>

namespace madeline_cube {
namespace {

float Clamp(float value, float min_value, float max_value) {
    if (value < min_value) return min_value;
    if (value > max_value) return max_value;
    return value;
}

float Lerp(float a, float b, float t) {
    return a + ((b - a) * t);
}

float Lerp3(float a, float b, float c, float d, float t) {
    if (t < (1.0f / 3.0f)) {
        return Lerp(a, b, t * 3.0f);
    }
    if (t < (2.0f / 3.0f)) {
        return Lerp(b, c, (t - (1.0f / 3.0f)) * 3.0f);
    }
    return Lerp(c, d, (t - (2.0f / 3.0f)) * 3.0f);
}

float LengthXZ(const Vec3& value) {
    return std::sqrt((value.x * value.x) + (value.z * value.z));
}

Vec3 NormalizeXZ(const Vec3& value) {
    const float len = LengthXZ(value);
    if (len <= 0.0001f) {
        return {0.0f, 0.0f, 1.0f};
    }
    return {value.x / len, 0.0f, value.z / len};
}

Vec3 RotateAroundUp(const Vec3& direction, float radians) {
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    return NormalizeXZ({
        (direction.x * cosine) - (direction.z * sine),
        0.0f,
        (direction.x * sine) + (direction.z * cosine),
    });
}

float FollowAlpha(float decay, float delta_seconds) {
    return 1.0f - std::pow(decay, delta_seconds);
}

float Snap(float value, float interval) {
    return std::round(value / interval) * interval;
}

Vec3 DesiredLookAt(const CameraState& camera, const CameraConfig& config) {
    return {
        camera.origin.x,
        camera.origin.y + config.look_at_height,
        camera.origin.z,
    };
}

Vec3 DesiredPosition(const CameraState& camera, const CameraConfig& config) {
    const Vec3 look_at = DesiredLookAt(camera, config);
    const float distance = Lerp3(3.0f, 6.0f, 11.0f, 11.0f, camera.target_distance);
    const float height = Lerp3(0.1f, 3.0f, 8.0f, 18.0f, camera.target_distance);
    return {
        look_at.x - (camera.target_forward.x * distance),
        look_at.y + height,
        look_at.z - (camera.target_forward.z * distance),
    };
}

}  // namespace

CameraController::CameraController(CameraConfig config) : config_(config) {}

void CameraController::Reset(CameraState& camera, const Vec3& player_position) const {
    camera.origin = player_position;
    camera.target_forward = NormalizeXZ(camera.target_forward);
    camera.target = DesiredLookAt(camera, config_);
    camera.position = DesiredPosition(camera, config_);
    camera.initialized = true;
}

void CameraController::Step(
    CameraState& camera,
    const Vec3& player_position,
    bool climbing,
    const CameraInput& input,
    float delta_seconds
) const {
    if (!camera.initialized) {
        Reset(camera, player_position);
    }

    if (input.orbit != 0.0f) {
        camera.target_forward = RotateAroundUp(
            camera.target_forward,
            -input.orbit * config_.rotation_speed * delta_seconds
        );
    }

    if (input.zoom != 0.0f) {
        camera.target_distance = Clamp(camera.target_distance + (input.zoom * delta_seconds), 0.0f, 1.0f);
    } else {
        const float interval = config_.zoom_snap_interval;
        const float remainder = std::fmod(camera.target_distance, interval);
        if (remainder < config_.zoom_snap_threshold ||
            remainder > interval - config_.zoom_snap_threshold) {
            const float snapped = Clamp(Snap(camera.target_distance, interval), 0.0f, 1.0f);
            const float max_delta = delta_seconds * 0.5f;
            if (camera.target_distance < snapped) {
                camera.target_distance = camera.target_distance + max_delta > snapped
                    ? snapped
                    : camera.target_distance + max_delta;
            } else {
                camera.target_distance = camera.target_distance - max_delta < snapped
                    ? snapped
                    : camera.target_distance - max_delta;
            }
        }
    }

    camera.origin.x = player_position.x;
    camera.origin.z = player_position.z;

    const float vertical_pad = climbing ? 0.0f : config_.vertical_pad;
    float target_y = camera.origin.y;
    if (player_position.y <= camera.origin.y) {
        target_y = player_position.y;
    } else if (player_position.y > camera.origin.y + vertical_pad) {
        target_y = player_position.y - vertical_pad;
    }

    if (camera.origin.y != target_y) {
        const float alpha = FollowAlpha(config_.vertical_follow_decay, delta_seconds);
        camera.origin.y += (target_y - camera.origin.y) * alpha;
    }

    const Vec3 desired_look_at = DesiredLookAt(camera, config_);
    const Vec3 desired_position = DesiredPosition(camera, config_);
    const float alpha = FollowAlpha(config_.camera_follow_decay, delta_seconds);

    camera.position.x += (desired_position.x - camera.position.x) * alpha;
    camera.position.y += (desired_position.y - camera.position.y) * alpha;
    camera.position.z += (desired_position.z - camera.position.z) * alpha;
    camera.target = desired_look_at;
}

}  // namespace madeline_cube
