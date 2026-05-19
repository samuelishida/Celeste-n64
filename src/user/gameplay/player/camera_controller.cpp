#include "gameplay/player/camera_controller.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>

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

// Clamp + linear map: maps value from [in_min, in_max] to [out_min, out_max],
// clamping the input first.
float ClampedMap(float value, float in_min, float in_max,
                 float out_min, float out_max) {
    const float range = in_max - in_min;
    if (range <= 0.0f) return out_min;
    const float t = Clamp((value - in_min) / range, 0.0f, 1.0f);
    return Lerp(out_min, out_max, t);
}

// Approach target at a given rate. Returns new value after moving toward target.
float Approach(float current, float target, float max_step) {
    if (current < target) {
        return current + max_step > target ? target : current + max_step;
    }
    return current - max_step < target ? target : current - max_step;
}

float LengthXZ(const Vec3& value) {
    return std::sqrt((value.x * value.x) + (value.z * value.z));
}

uint32_t FloatBits(float value) {
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

bool IsFiniteBits(float value) {
    return (FloatBits(value) & 0x7F800000u) != 0x7F800000u;
}

float FlushSubnormalBits(float value) {
    const uint32_t bits = FloatBits(value);
    const uint32_t exponent = bits & 0x7F800000u;
    const uint32_t mantissa = bits & 0x007FFFFFu;
    return exponent == 0u && mantissa != 0u ? 0.0f : value;
}

float SanitizeCoordinateBits(float value) {
    return IsFiniteBits(value) ? FlushSubnormalBits(value) : 0.0f;
}

Vec3 NormalizeXZ(const Vec3& value) {
    if (!IsFiniteBits(value.x) || !IsFiniteBits(value.z)) {
        return {0.0f, 0.0f, 1.0f};
    }

    const Vec3 sanitized = {
        FlushSubnormalBits(value.x),
        0.0f,
        FlushSubnormalBits(value.z),
    };
    const float len = LengthXZ(sanitized);
    if (!std::isfinite(len) || len <= 0.0001f) {
        return {0.0f, 0.0f, 1.0f};
    }
    return {
        FlushSubnormalBits(sanitized.x / len),
        0.0f,
        FlushSubnormalBits(sanitized.z / len),
    };
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
    const float distance = Lerp3(30.0f, 60.0f, 110.0f, 110.0f, camera.target_distance);
    const float height = Lerp3(1.0f, 30.0f, 80.0f, 180.0f, camera.target_distance);
    return {
        look_at.x - (camera.target_forward.x * distance),
        look_at.y + height,
        look_at.z - (camera.target_forward.z * distance),
    };
}

}  // namespace

CameraController::CameraController(CameraConfig config) : config_(config) {}

void CameraController::Reset(CameraState& camera, const Vec3& player_position) {
    camera.origin = {
        SanitizeCoordinateBits(player_position.x),
        SanitizeCoordinateBits(player_position.y),
        SanitizeCoordinateBits(player_position.z),
    };

    // OG: on respawn, cameraTargetForward = storedCameraForward,
    //     cameraTargetDistance = storedCameraDistance.
    // On cold start (no stored values yet), use default forward (0,0,1)
    // and default distance (0.5).
    if (!camera.initialized && !has_stored_) {
        camera.target_forward = {0.0f, 0.0f, 1.0f};
        camera.target_distance = 0.5f;
    } else if (has_stored_) {
        camera.target_forward = NormalizeXZ(stored_forward_);
        camera.target_distance = Clamp(stored_distance_, 0.0f, 1.0f);
    }
    // else: respawn but no stored yet (shouldn't happen, but keep current values)

    camera.target_forward = NormalizeXZ(camera.target_forward);
    camera.target = DesiredLookAt(camera, config_);
    camera.position = DesiredPosition(camera, config_);
    camera.fov_multiplier = 1.0f;
    camera.initialized = true;
}

void CameraController::Step(
    CameraState& camera,
    const Vec3& player_position,
    bool climbing,
    bool grounded,
    float horizontal_speed,
    const CameraInput& input,
    float delta_seconds,
    const Room* room
) {
    if (!camera.initialized) {
        Reset(camera, player_position);
    }

    // Store forward/distance so they persist across respawn (OG: static
    // storedCameraForward/storedCameraDistance).
    stored_forward_ = camera.target_forward;
    stored_distance_ = camera.target_distance;
    has_stored_ = true;

    // --- Orbit ---
    if (input.orbit != 0.0f) {
        camera.target_forward = RotateAroundUp(
            camera.target_forward,
            -input.orbit * config_.rotation_speed * delta_seconds
        );
    }

    // --- Zoom ---
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

    // --- XZ follow (immediate, no smoothing) ---
    camera.origin.x = player_position.x;
    camera.origin.z = player_position.z;

    // --- Y follow ---
    // OG behavior:
    //   - Grounded: snap Y to player Y immediately
    //   - Airborne + player below camera: snap down immediately
    //   - Airborne + player above pad: rise with fast approach
    //   - Airborne + player within pad: hold current Y
    const float vertical_pad = climbing ? 0.0f : config_.vertical_pad;
    float target_y = camera.origin.y;

    if (grounded) {
        // When on ground, camera Y tracks player exactly (no lag).
        target_y = player_position.y;
    } else if (player_position.y < camera.origin.y) {
        // Player fell below camera origin: snap down immediately.
        target_y = player_position.y;
    } else if (player_position.y > camera.origin.y + vertical_pad) {
        // Player rose above the dead zone: shift target up by pad amount.
        target_y = player_position.y - vertical_pad;
    }

    if (camera.origin.y != target_y) {
        // OG: vertical follow uses decay 0.001 => (1 - 0.001^dt) alpha.
        // This is extremely fast (~99.2% per frame at 60fps), almost a snap.
        const float alpha = FollowAlpha(config_.vertical_follow_decay, delta_seconds);
        camera.origin.y += (target_y - camera.origin.y) * alpha;
    }

    // --- Compute desired camera position ---
    const Vec3 desired_look_at = DesiredLookAt(camera, config_);
    Vec3 desired_position = DesiredPosition(camera, config_);

    // --- Wall collision (OG: SolidRayCast from lookAt toward camera pos) ---
    if (room != nullptr) {
        constexpr float kCameraSkin = 0.5f;
        constexpr float kCeilingProbe = 5.0f;
        const Vec3 diff = {
            desired_position.x - desired_look_at.x,
            desired_position.y - desired_look_at.y,
            desired_position.z - desired_look_at.z,
        };
        float distance = std::sqrt(
            (diff.x * diff.x) + (diff.y * diff.y) + (diff.z * diff.z));

        if (distance > 0.0001f && std::isfinite(distance)) {
            const Vec3 direction = {diff.x / distance, diff.y / distance, diff.z / distance};

            // OG: reduce distance by near plane to account for near-plane cutoff.
            if (distance > config_.near_plane + 1.0f) {
                distance -= config_.near_plane;
            }

            const GroundHit obstruction = RaycastRoomSource(
                *room, desired_look_at, direction, distance, BackfacePolicy::Ignore);
            if (obstruction.hit) {
                desired_position = {
                    obstruction.point.x + (obstruction.normal.x * kCameraSkin),
                    obstruction.point.y + (obstruction.normal.y * kCameraSkin),
                    obstruction.point.z + (obstruction.normal.z * kCameraSkin),
                };
            }
        }

        // Push down from ceilings (OG: SolidRayCast upward from camera pos).
        const CeilingHit ceiling = QueryCeilingSource(*room, desired_position, kCeilingProbe);
        if (ceiling.hit) {
            desired_position.y = ceiling.point.y - kCeilingProbe;
        }
    }

    // --- Smooth position follow (OG: 1 - pow(0.01, dt)) ---
    const float alpha = FollowAlpha(config_.camera_follow_decay, delta_seconds);

    camera.position.x += (desired_position.x - camera.position.x) * alpha;
    camera.position.y += (desired_position.y - camera.position.y) * alpha;
    camera.position.z += (desired_position.z - camera.position.z) * alpha;
    camera.target = desired_look_at;

    // --- Dynamic FOV (OG: ClampedMap + Approach) ---
    // When horizontal speed exceeds fov_speed_min (1.2x max run speed),
    // FOV widens from 1.0x to 1.2x, smoothly approached.
    const float target_fov_mult = ClampedMap(
        horizontal_speed,
        config_.fov_speed_min,
        config_.fov_speed_max,
        1.0f,
        config_.fov_max_multiplier);
    camera.fov_multiplier = Approach(
        camera.fov_multiplier,
        target_fov_mult,
        delta_seconds / 4.0f);
}

}  // namespace madeline_cube