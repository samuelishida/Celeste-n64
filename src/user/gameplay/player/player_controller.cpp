#include "gameplay/player/player_controller.hpp"

#include <algorithm>
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

void StartJump(PlayerState& state, const MovementConfig& config, const Vec3& move_input) {
    state.velocity.y = config.jump_speed;
    state.hold_jump_speed = config.jump_speed;
    state.hold_jump_time_remaining = config.jump_hold_time;
    state.coyote_time_remaining = 0.0f;
    state.auto_jump = false;
    state.jump_cut_applied = false;
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

void ApplyJumpAndGravity(
    PlayerState& state,
    const PlayerInput& input,
    const Vec3& move_input,
    const MovementConfig& config,
    const MovementProfile& profile,
    float delta_seconds
) {
    const bool can_jump = state.grounded || state.coyote_time_remaining > 0.0f;
    if (state.jump_buffer_remaining > 0.0f && can_jump) {
        state.jump_buffer_remaining = 0.0f;
        StartJump(state, config, move_input);
        return;
    }

    if (!input.jump_held && state.velocity.y > 0.0f && !state.jump_cut_applied) {
        state.velocity.y *= profile.jump_cut_multiplier;
        state.jump_cut_applied = true;
    }
    float gravity = profile.fall_gravity;
    if (state.velocity.y > profile.apex_threshold) {
        gravity = profile.rise_gravity;
    } else if (std::fabs(state.velocity.y) <= profile.apex_threshold) {
        gravity = profile.apex_gravity;
    }
    state.velocity.y = MoveToward(
        state.velocity.y,
        profile.max_fall_speed,
        gravity * delta_seconds
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
        state.coyote_time_remaining = profile_.coyote_time;
        state.wall_grab_time_remaining = config_.wall_grab_time;
        if (state.dash_reset_cooldown_remaining <= 0.0f) {
            state.air_dash_available = true;
        }
        state.stamina = profile_.stamina_max;
        state.climb_exhausted = false;
    } else {
        state.coyote_time_remaining = MoveToward(state.coyote_time_remaining, 0.0f, delta_seconds);
    }

    if (input.jump_pressed) {
        state.jump_buffer_remaining = profile_.jump_buffer_time;
    } else {
        state.jump_buffer_remaining = MoveToward(state.jump_buffer_remaining, 0.0f, delta_seconds);
    }

    state.dash_cooldown_remaining = MoveToward(state.dash_cooldown_remaining, 0.0f, delta_seconds);
    state.dash_reset_cooldown_remaining = MoveToward(state.dash_reset_cooldown_remaining, 0.0f, delta_seconds);
    state.no_dash_jump_remaining = MoveToward(state.no_dash_jump_remaining, 0.0f, delta_seconds);
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
        if (!input.climb_held || state.stamina <= 0.0f) {
            state.movement_state = PlayerMovementState::Normal;
            state.locomotion_state = LocomotionState::Fall;
            if (state.stamina <= 0.0f) state.climb_exhausted = true;
        } else if (input.jump_pressed) {
            state.movement_state = PlayerMovementState::Normal;
            const bool neutral = !has_move_input;
            state.target_facing = {-state.target_facing.x, 0.0f, -state.target_facing.z};
            const float horizontal = neutral
                ? profile_.neutral_wall_jump_horizontal_speed
                : profile_.wall_jump_horizontal_speed;
            state.velocity.x = state.target_facing.x * horizontal;
            state.velocity.y = neutral
                ? profile_.neutral_wall_jump_vertical_speed
                : profile_.wall_jump_vertical_speed;
            state.velocity.z = state.target_facing.z * horizontal;
            state.stamina = std::max(0.0f, state.stamina - profile_.climb_jump_cost);
            state.locomotion_state = LocomotionState::Jump;
            state.hold_jump_speed = config_.jump_speed;
            state.hold_jump_time_remaining = config_.jump_hold_time;
        } else {
            state.velocity.x = 0.0f;
            state.velocity.z = 0.0f;
            state.velocity.y = input.move.y * profile_.climb_speed;
            const float drain = input.move.y > 0.0f
                ? profile_.climb_up_drain_per_second
                : profile_.climb_hold_drain_per_second;
            state.stamina = std::max(0.0f, state.stamina - drain * delta_seconds);
            state.locomotion_state = LocomotionState::Climb;
        }
        return;
    }

    if (input.climb_held &&
        state.climb.cooldown_remaining <= 0.0f &&
        !state.climb_exhausted &&
        state.stamina > 0.0f &&
        !state.grounded &&
        (state.wall_left || state.wall_right)) {
        state.movement_state = PlayerMovementState::Climbing;
        state.locomotion_state = LocomotionState::Climb;
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
        state.locomotion_state = LocomotionState::Dash;
        state.air_dash_available = false;
        state.auto_jump = false;
        state.dash_hitstop_remaining = profile_.dash_hitstop_time;
        state.dash_active_remaining = profile_.dash_active_time;
        state.dash_time_remaining = profile_.dash_hitstop_time + profile_.dash_active_time;
        state.dash_reset_cooldown_remaining = config_.dash_reset_cooldown;
        state.no_dash_jump_remaining = 0.10f;
        state.wall_grabbing = false;
        state.velocity = {};
    }

    if (!state.wall_grabbing) {
        if (state.movement_state == PlayerMovementState::Dashing) {
            state.dash_time_remaining -= delta_seconds;
            if (state.dash_hitstop_remaining > 0.0f) {
                state.dash_hitstop_remaining = MoveToward(state.dash_hitstop_remaining, 0.0f, delta_seconds);
                state.velocity = {};
                return;
            }
            state.dash_active_remaining = MoveToward(state.dash_active_remaining, 0.0f, delta_seconds);
            state.velocity.x = state.target_facing.x * profile_.dash_speed;
            state.velocity.y = 0.0f;
            state.velocity.z = state.target_facing.z * profile_.dash_speed;
            if (state.dash_time_remaining <= 0.0f) {
                if (!state.grounded) {
                    state.velocity.x *= config_.dash_end_speed_multiplier;
                    state.velocity.y *= config_.dash_end_speed_multiplier;
                    state.velocity.z *= config_.dash_end_speed_multiplier;
                }
                state.movement_state = PlayerMovementState::Normal;
                state.dash_cooldown_remaining = config_.dash_cooldown;
            } else {
                if (state.dashed_on_ground &&
                    state.coyote_time_remaining > 0.0f &&
                    state.no_dash_jump_remaining <= 0.0f &&
                    input.jump_pressed) {
                    state.movement_state = PlayerMovementState::Normal;
                    StartDashJump(state, config_, move_input);
                }
            }
        }

        if (state.movement_state == PlayerMovementState::Normal) {
            const Vec3 horizontal_velocity = {state.velocity.x, 0.0f, state.velocity.z};
            const float desired_speed = profile_.run_max_speed * AnalogMagnitude(raw_input_length);
            const Vec3 target_velocity = has_move_input
                ? Vec3{move_input.x * desired_speed, state.velocity.y, move_input.z * desired_speed}
                : Vec3{0.0f, state.velocity.y, 0.0f};
            if (state.grounded) {
                const float accel = has_move_input
                    ? profile_.ground_acceleration
                    : profile_.ground_deceleration;
                state.velocity = MoveTowardXZ(state.velocity, target_velocity, accel * delta_seconds);
                state.locomotion_state = has_move_input || LengthXZ(state.velocity) > kEpsilon
                    ? LocomotionState::Run
                    : LocomotionState::Idle;
                if (has_move_input) {
                    state.target_facing = move_input;
                    state.facing = move_input;
                }
            } else {
                const float accel = has_move_input &&
                    DotXZ(NormalizeXZ(horizontal_velocity, state.target_facing), move_input) < 0.0f
                    ? profile_.air_turn_acceleration
                    : profile_.air_acceleration;
                if (has_move_input) {
                    state.velocity = MoveTowardXZ(state.velocity, target_velocity, accel * delta_seconds);
                    state.target_facing = move_input;
                }
                state.locomotion_state = state.velocity.y > 0.0f ? LocomotionState::Jump : LocomotionState::Fall;
            }

            ApplyJumpAndGravity(state, input, move_input, config_, profile_, delta_seconds);
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
