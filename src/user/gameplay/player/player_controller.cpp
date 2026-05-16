#include "gameplay/player/player_controller.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>

namespace madeline_cube {
namespace {

constexpr float kEpsilon = 0.0001f;

float Clamp(float value, float min_value, float max_value) {
    if (value < min_value) return min_value;
    if (value > max_value) return max_value;
    return value;
}

float Length(float x, float y) {
    return std::sqrt((x * x) + (y * y));
}

float LengthXZ(const Vec3& value) {
    return Length(value.x, value.z);
}

float DotXZ(const Vec3& a, const Vec3& b) {
    return (a.x * b.x) + (a.z * b.z);
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

Vec3 NormalizeXZ(const Vec3& value, const Vec3& fallback = {0.0f, 0.0f, 1.0f}) {
    if (!IsFiniteBits(value.x) || !IsFiniteBits(value.z)) {
        return fallback;
    }

    const Vec3 sanitized = {
        FlushSubnormalBits(value.x),
        0.0f,
        FlushSubnormalBits(value.z),
    };
    const float len = LengthXZ(sanitized);
    if (!std::isfinite(len) || len <= kEpsilon) {
        return fallback;
    }
    return {
        FlushSubnormalBits(sanitized.x / len),
        0.0f,
        FlushSubnormalBits(sanitized.z / len),
    };
}

float MoveToward(float current, float target, float max_delta) {
    if (current < target) {
        const float next = current + max_delta;
        return next > target ? target : next;
    }
    const float next = current - max_delta;
    return next < target ? target : next;
}

Vec3 MoveTowardXZ(const Vec3& current, const Vec3& target, float max_delta) {
    const float dx = target.x - current.x;
    const float dz = target.z - current.z;
    const float distance = Length(dx, dz);
    if (distance <= max_delta || distance <= kEpsilon) {
        return {target.x, current.y, target.z};
    }
    const float scale = max_delta / distance;
    return {current.x + (dx * scale), current.y, current.z + (dz * scale)};
}

float AngleXZ(const Vec3& value) {
    return std::atan2(value.z, value.x);
}

Vec3 DirectionFromAngle(float angle) {
    return {std::cos(angle), 0.0f, std::sin(angle)};
}

float ApproachAngle(float from, float to, float max_delta) {
    constexpr float kPi = 3.14159265358979323846f;
    constexpr float kTau = kPi * 2.0f;
    float diff = std::fmod(to - from + kPi, kTau);
    if (diff < 0.0f) diff += kTau;
    diff -= kPi;
    if (diff > max_delta) diff = max_delta;
    if (diff < -max_delta) diff = -max_delta;
    return from + diff;
}

Vec3 RotateTowardXZ(const Vec3& from, const Vec3& to, float max_delta) {
    return DirectionFromAngle(ApproachAngle(AngleXZ(from), AngleXZ(to), max_delta));
}

float AnalogMagnitude(float raw_length) {
    const float t = Clamp((raw_length - 0.4f) / (0.92f - 0.4f), 0.0f, 1.0f);
    return 0.3f + ((1.0f - 0.3f) * t);
}

Vec3 RelativeMoveInput(const Vec2& move, const Vec3& camera_forward, const Vec3& fallback_facing) {
    const float input_length = Length(move.x, move.y);
    if (input_length <= kEpsilon) {
        return {};
    }

    const Vec3 forward = NormalizeXZ(camera_forward, fallback_facing);
    const Vec3 right = {forward.z, 0.0f, -forward.x};
    const float normalized_x = move.x / input_length;
    const float normalized_y = move.y / input_length;
    return NormalizeXZ({
        (right.x * normalized_x) + (forward.x * normalized_y),
        0.0f,
        (right.z * normalized_x) + (forward.z * normalized_y),
    }, fallback_facing);
}

void SetDashVelocity(PlayerState& state, const MovementConfig& config) {
    if (state.dashed_on_ground) {
        state.velocity.x = state.target_facing.x * config.dash_speed;
        state.velocity.y = 0.0f;
        state.velocity.z = state.target_facing.z * config.dash_speed;
        return;
    }

    const float horizontal = 1.0f;
    const float vertical = 0.4f;
    const float norm = std::sqrt((horizontal * horizontal) + (vertical * vertical));
    state.velocity.x = state.target_facing.x * (horizontal / norm) * config.dash_speed;
    state.velocity.y = (vertical / norm) * config.dash_speed;
    state.velocity.z = state.target_facing.z * (horizontal / norm) * config.dash_speed;
}

void StartJump(PlayerState& state, const MovementConfig& config, const Vec3& move_input) {
    state.velocity.y = config.jump_speed;
    state.hold_jump_speed = config.jump_speed;
    state.hold_jump_time_remaining = config.jump_hold_time;
    state.coyote_time_remaining = 0.0f;
    state.auto_jump = false;
    state.grounded = false;
    if (state.platform_carry.time_remaining > 0.0f) {
        state.velocity.x += state.platform_carry.stored_velocity.x;
        state.velocity.y += state.platform_carry.stored_velocity.y;
        state.velocity.z += state.platform_carry.stored_velocity.z;
        state.platform_carry = {};
    }

    if (LengthXZ(move_input) > kEpsilon) {
        state.target_facing = NormalizeXZ(move_input, state.target_facing);
        state.last_facing = state.target_facing;
        state.velocity.x += state.target_facing.x * config.jump_xy_boost;
        state.velocity.z += state.target_facing.z * config.jump_xy_boost;
    }
}

void StartDashJump(PlayerState& state, const MovementConfig& config, const Vec3& move_input) {
    state.velocity.y = config.dash_jump_speed;
    state.hold_jump_speed = config.dash_jump_hold_speed;
    state.hold_jump_time_remaining = config.dash_jump_hold_time;
    state.coyote_time_remaining = 0.0f;
    state.auto_jump = false;
    state.air_dash_available = true;

    if (LengthXZ(move_input) > kEpsilon) {
        state.target_facing = NormalizeXZ(move_input, state.target_facing);
        state.last_facing = state.target_facing;
        state.velocity.x += state.target_facing.x * config.dash_jump_xy_boost;
        state.velocity.z += state.target_facing.z * config.dash_jump_xy_boost;
    }
}

void StartSkidJump(PlayerState& state, const MovementConfig& config) {
    state.velocity.x = state.target_facing.x * config.skid_jump_xy_speed;
    state.velocity.y = config.skid_jump_speed;
    state.velocity.z = state.target_facing.z * config.skid_jump_xy_speed;
    state.hold_jump_speed = config.skid_jump_speed;
    state.hold_jump_time_remaining = config.skid_jump_hold_time;
    state.coyote_time_remaining = 0.0f;
}

void ApplyJumpAndGravity(
    PlayerState& state,
    const PlayerInput& input,
    const Vec3& move_input,
    const MovementConfig& config,
    float delta_seconds
) {
    const bool can_jump = state.grounded || state.coyote_time_remaining > 0.0f;
    if (state.jump_buffer_remaining > 0.0f && can_jump) {
        state.jump_buffer_remaining = 0.0f;
        StartJump(state, config, move_input);
        return;
    }

    if (state.hold_jump_time_remaining > 0.0f && (state.auto_jump || input.jump_held)) {
        if (state.velocity.y < state.hold_jump_speed) {
            state.velocity.y = state.hold_jump_speed;
        }
        return;
    }

    float gravity_multiplier = 1.0f;
    if ((input.jump_held || state.auto_jump) && std::fabs(state.velocity.y) < config.half_gravity_threshold) {
        gravity_multiplier = 0.5f;
    } else {
        state.auto_jump = false;
    }

    state.velocity.y = MoveToward(
        state.velocity.y,
        config.max_fall_speed,
        config.gravity * gravity_multiplier * delta_seconds
    );
    state.hold_jump_time_remaining = 0.0f;
}

}  // namespace

PlayerController::PlayerController(MovementConfig config) : config_(config) {}

void PlayerController::Step(
    PlayerState& state,
    const PlayerInput& input,
    const Vec3& camera_forward,
    float delta_seconds
) const {
    if (delta_seconds <= 0.0f) {
        return;
    }

    const StepContext context = TimerInputPhase(state, input, camera_forward, delta_seconds);
    StatePhase(state, input, context, delta_seconds);
    LateContactPhase(state);
}

PlayerController::StepContext PlayerController::TimerInputPhase(
    PlayerState& state,
    const PlayerInput& input,
    const Vec3& camera_forward,
    float delta_seconds
) const {
    StepContext context;
    if (delta_seconds <= 0.0f) {
        return context;
    }

    context.raw_input_length = Clamp(Length(input.move.x, input.move.y), 0.0f, 1.0f);
    context.move_input = RelativeMoveInput(input.move, camera_forward, state.target_facing);
    context.has_move_input = context.raw_input_length > kEpsilon;
    state.contact.previous_velocity = state.velocity;
    if (context.has_move_input) {
        state.last_facing = context.move_input;
    }

    if (state.grounded) {
        state.coyote_time_remaining = config_.coyote_time;
        state.wall_grab_time_remaining = config_.wall_grab_time;
        if (state.dash_reset_cooldown_remaining <= 0.0f) {
            state.air_dash_available = true;
        }
    } else {
        state.coyote_time_remaining = MoveToward(state.coyote_time_remaining, 0.0f, delta_seconds);
    }

    if (input.jump_pressed) {
        state.jump_buffer_remaining = config_.jump_buffer_time;
    } else {
        state.jump_buffer_remaining = MoveToward(state.jump_buffer_remaining, 0.0f, delta_seconds);
    }

    state.dash_cooldown_remaining = MoveToward(state.dash_cooldown_remaining, 0.0f, delta_seconds);
    state.dash_reset_cooldown_remaining = MoveToward(state.dash_reset_cooldown_remaining, 0.0f, delta_seconds);
    state.no_dash_jump_remaining = MoveToward(state.no_dash_jump_remaining, 0.0f, delta_seconds);
    state.no_skid_jump_remaining = MoveToward(state.no_skid_jump_remaining, 0.0f, delta_seconds);
    state.hold_jump_time_remaining = MoveToward(state.hold_jump_time_remaining, 0.0f, delta_seconds);
    state.wall_jump_cooldown_remaining = MoveToward(state.wall_jump_cooldown_remaining, 0.0f, delta_seconds);
    state.climb.cooldown_remaining = MoveToward(state.climb.cooldown_remaining, 0.0f, delta_seconds);
    state.no_move_time_remaining = MoveToward(state.no_move_time_remaining, 0.0f, delta_seconds);
    state.contact.ground_snap_cooldown_remaining = MoveToward(
        state.contact.ground_snap_cooldown_remaining,
        0.0f,
        delta_seconds
    );
    state.platform_carry.time_remaining = MoveToward(state.platform_carry.time_remaining, 0.0f, delta_seconds);

    return context;
}

void PlayerController::StatePhase(
    PlayerState& state,
    const PlayerInput& input,
    const StepContext& context,
    float delta_seconds
) const {
    const Vec3& move_input = context.move_input;
    const bool has_move_input = context.has_move_input;
    const float raw_input_length = context.raw_input_length;
    if (delta_seconds <= 0.0f) {
        return;
    }

    if (state.movement_state == PlayerMovementState::Climbing) {
        if (!input.climb_held) {
            state.movement_state = PlayerMovementState::Normal;
        } else if (input.jump_pressed) {
            state.movement_state = PlayerMovementState::Normal;
            state.target_facing = {-state.target_facing.x, 0.0f, -state.target_facing.z};
            state.velocity.x = state.target_facing.x * config_.wall_jump_speed_x;
            state.velocity.y = config_.wall_jump_speed_y;
            state.velocity.z = state.target_facing.z * config_.wall_jump_speed_x;
            state.hold_jump_speed = config_.jump_speed;
            state.hold_jump_time_remaining = config_.jump_hold_time;
        } else {
            state.velocity.x = 0.0f;
            state.velocity.z = 0.0f;
            state.velocity.y = -input.move.y * config_.climb_speed;
        }
        return;
    }

    if (input.climb_held &&
        state.climb.cooldown_remaining <= 0.0f &&
        !state.grounded &&
        (state.wall_left || state.wall_right)) {
        state.movement_state = PlayerMovementState::Climbing;
        state.velocity = {};
        return;
    }

    // Wall slide remains as a forgiving fallback when the dedicated climb input
    // is not held.
    const bool can_wall_grab = !state.grounded && state.velocity.y < 0.0f &&
                               (state.wall_left || state.wall_right) &&
                               state.wall_grab_time_remaining > 0.0f &&
                               state.wall_jump_cooldown_remaining <= 0.0f;
    if (can_wall_grab && input.jump_held) {
        state.wall_grabbing = true;
    } else if (!input.jump_held || state.grounded) {
        state.wall_grabbing = false;
    }

    if (state.wall_grabbing) {
        state.wall_grab_time_remaining = MoveToward(state.wall_grab_time_remaining, 0.0f, delta_seconds);
        state.velocity.y = -config_.wall_slide_speed;
        state.velocity.x = 0.0f;
        state.velocity.z = 0.0f;

        if (input.jump_pressed) {
            state.wall_grabbing = false;
            state.wall_jump_cooldown_remaining = config_.wall_jump_cooldown;
            const float dir_x = state.wall_left ? 1.0f : -1.0f;
            state.target_facing = {dir_x, 0.0f, 0.0f};
            state.last_facing = state.target_facing;
            state.velocity.x = dir_x * config_.wall_jump_speed_x;
            state.velocity.y = config_.wall_jump_speed_y;
            state.velocity.z = 0.0f;
            state.hold_jump_speed = config_.jump_speed;
            state.hold_jump_time_remaining = config_.jump_hold_time;
        }
    }

    if (!state.wall_grabbing &&
        !state.grounded &&
        input.jump_pressed &&
        (state.wall_left || state.wall_right)) {
        const float dir_x = state.wall_left ? 1.0f : -1.0f;
        state.target_facing = {dir_x, 0.0f, 0.0f};
        state.last_facing = state.target_facing;
        state.velocity.x = dir_x * config_.wall_jump_speed_x;
        state.velocity.y = config_.wall_jump_speed_y;
        state.velocity.z = 0.0f;
        state.hold_jump_speed = config_.jump_speed;
        state.hold_jump_time_remaining = config_.jump_hold_time;
        state.wall_jump_cooldown_remaining = config_.wall_jump_cooldown;
    }

    if (!state.wall_grabbing &&
        input.dash_pressed &&
        state.air_dash_available &&
        state.dash_cooldown_remaining <= 0.0f) {
        if (has_move_input) {
            state.target_facing = move_input;
        }
        state.target_facing = NormalizeXZ(state.target_facing, state.last_facing);
        state.last_facing = state.target_facing;
        state.dashed_on_ground = state.grounded;
        state.movement_state = PlayerMovementState::Dashing;
        state.air_dash_available = false;
        state.auto_jump = true;
        state.dash_time_remaining = config_.dash_duration;
        state.dash_reset_cooldown_remaining = config_.dash_reset_cooldown;
        state.no_dash_jump_remaining = 0.10f;
        state.wall_grabbing = false;
        SetDashVelocity(state, config_);
    }

    if (!state.wall_grabbing) {
        if (state.movement_state == PlayerMovementState::Dashing) {
            state.dash_time_remaining -= delta_seconds;
            if (state.dash_time_remaining <= 0.0f) {
                if (!state.grounded) {
                    state.velocity.x *= config_.dash_end_speed_multiplier;
                    state.velocity.y *= config_.dash_end_speed_multiplier;
                    state.velocity.z *= config_.dash_end_speed_multiplier;
                }
                state.movement_state = PlayerMovementState::Normal;
                state.dash_cooldown_remaining = config_.dash_cooldown;
            } else {
                if (has_move_input && DotXZ(move_input, state.target_facing) >= -0.2f) {
                    state.target_facing = RotateTowardXZ(
                        state.target_facing,
                        move_input,
                        config_.dash_rotate_speed * delta_seconds
                    );
                    state.last_facing = state.target_facing;
                    SetDashVelocity(state, config_);
                }

                if (state.dashed_on_ground &&
                    state.coyote_time_remaining > 0.0f &&
                    state.no_dash_jump_remaining <= 0.0f &&
                    input.jump_pressed) {
                    state.movement_state = PlayerMovementState::Normal;
                    StartDashJump(state, config_, move_input);
                }
            }
        }

        if (state.movement_state == PlayerMovementState::Skidding) {
            const bool cancel_skid = !state.grounded ||
                                     !has_move_input ||
                                     DotXZ(move_input, state.target_facing) < 0.7f;
            if (cancel_skid) {
                state.movement_state = PlayerMovementState::Normal;
            } else if (state.no_skid_jump_remaining <= 0.0f && input.jump_pressed) {
                state.movement_state = PlayerMovementState::Normal;
                StartSkidJump(state, config_);
            } else {
                const Vec3 horizontal_velocity = {state.velocity.x, 0.0f, state.velocity.z};
                const Vec3 velocity_direction = NormalizeXZ(horizontal_velocity, state.target_facing);
                const bool dot_matches = DotXZ(velocity_direction, state.target_facing) >= 0.7f;
                const float acceleration = dot_matches
                    ? config_.skidding_acceleration
                    : config_.skidding_start_acceleration;
                const Vec3 target_velocity = {
                    move_input.x * config_.run_speed,
                    state.velocity.y,
                    move_input.z * config_.run_speed,
                };
                state.velocity = MoveTowardXZ(state.velocity, target_velocity, acceleration * delta_seconds);
                if (dot_matches && LengthXZ(state.velocity) >= config_.end_skid_speed) {
                    state.movement_state = PlayerMovementState::Normal;
                }
            }
        }

        if (state.movement_state == PlayerMovementState::Normal) {
            const Vec3 horizontal_velocity = {state.velocity.x, 0.0f, state.velocity.z};
            const float horizontal_speed = LengthXZ(horizontal_velocity);

            if (!has_move_input) {
                float friction = config_.friction;
                if (!state.grounded) {
                    friction *= config_.air_friction_mult;
                }
                const Vec3 zero = {0.0f, state.velocity.y, 0.0f};
                state.velocity = MoveTowardXZ(state.velocity, zero, friction * delta_seconds);
            } else if (state.grounded) {
                float max_speed = config_.run_speed * AnalogMagnitude(raw_input_length);
                const float true_max_speed = config_.run_speed;
                float acceleration = config_.acceleration;

                if (horizontal_speed >= true_max_speed &&
                    DotXZ(move_input, NormalizeXZ(horizontal_velocity, state.target_facing)) >= 0.7f) {
                    acceleration = config_.past_max_deceleration;
                }

                if (horizontal_speed >= config_.rotate_threshold) {
                    const Vec3 velocity_direction = NormalizeXZ(horizontal_velocity, state.target_facing);
                    if (DotXZ(move_input, velocity_direction) <= config_.skid_dot_threshold) {
                        state.facing = move_input;
                        state.target_facing = move_input;
                        state.last_facing = move_input;
                        state.movement_state = PlayerMovementState::Skidding;
                        state.no_skid_jump_remaining = 0.10f;
                    } else {
                        const float rotate_speed = horizontal_speed > true_max_speed
                            ? config_.rotate_speed_above_max
                            : config_.rotate_speed;
                        state.target_facing = RotateTowardXZ(
                            state.target_facing,
                            move_input,
                            rotate_speed * delta_seconds
                        );
                        const float next_speed = MoveToward(horizontal_speed, max_speed, acceleration * delta_seconds);
                        state.velocity.x = state.target_facing.x * next_speed;
                        state.velocity.z = state.target_facing.z * next_speed;
                    }
                } else {
                    const Vec3 target_velocity = {
                        move_input.x * max_speed,
                        state.velocity.y,
                        move_input.z * max_speed,
                    };
                    state.velocity = MoveTowardXZ(state.velocity, target_velocity, acceleration * delta_seconds);
                    state.target_facing = move_input;
                }
            } else {
                float acceleration = config_.acceleration;
                if (horizontal_speed >= config_.run_speed) {
                    const float align = DotXZ(
                        move_input,
                        NormalizeXZ(horizontal_velocity, state.target_facing)
                    );
                    if (align >= 0.7f) {
                        const float facing_dot = DotXZ(move_input, state.target_facing);
                        const float t = Clamp((facing_dot + 1.0f) * 0.5f, 0.0f, 1.0f);
                        acceleration *= config_.air_accel_mult_max +
                                        ((config_.air_accel_mult_min - config_.air_accel_mult_max) * t);
                    }
                } else {
                    const float facing_dot = DotXZ(move_input, state.target_facing);
                    const float t = Clamp((facing_dot + 1.0f) * 0.5f, 0.0f, 1.0f);
                    acceleration *= config_.air_accel_mult_min +
                                    ((config_.air_accel_mult_max - config_.air_accel_mult_min) * t);
                }

                const Vec3 target_velocity = {
                    move_input.x * config_.run_speed,
                    state.velocity.y,
                    move_input.z * config_.run_speed,
                };
                state.velocity = MoveTowardXZ(state.velocity, target_velocity, acceleration * delta_seconds);
            }

            ApplyJumpAndGravity(state, input, move_input, config_, delta_seconds);
        }
    }

    if (!state.wall_grabbing && state.movement_state != PlayerMovementState::Dashing) {
        state.facing = RotateTowardXZ(state.facing, state.target_facing, 12.566371f * delta_seconds);
    }
}

void PlayerController::LateContactPhase(PlayerState& state) const {
    state.contact.was_grounded = state.grounded;
}

}  // namespace madeline_cube
