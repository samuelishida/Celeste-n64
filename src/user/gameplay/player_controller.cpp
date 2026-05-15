#include "player_controller.hpp"

#include <cmath>

namespace madeline_cube {
namespace {

float Length(float x, float y) {
    return std::sqrt((x * x) + (y * y));
}

float MoveToward(float current, float target, float max_delta) {
    if (current < target) {
        const float next = current + max_delta;
        return next > target ? target : next;
    }

    const float next = current - max_delta;
    return next < target ? target : next;
}

Vec3 NormalizedMoveDirection(const Vec2& move, const Vec3& fallback) {
    const float length = Length(move.x, move.y);
    if (length <= 0.0001f) {
        return fallback;
    }

    return {move.x / length, 0.0f, move.y / length};
}

}  // namespace

PlayerController::PlayerController(MovementConfig config) : config_(config) {}

void PlayerController::Step(PlayerState& state, const PlayerInput& input, float delta_seconds) const {
    if (delta_seconds <= 0.0f) {
        return;
    }

    const float input_length = Length(input.move.x, input.move.y);
    const bool has_move_input = input_length > 0.0001f;
    if (has_move_input) {
        state.last_facing = NormalizedMoveDirection(input.move, state.last_facing);
    }

    if (state.grounded) {
        state.air_dash_available = true;
    }

    if (input.jump_pressed && state.grounded) {
        state.velocity.y = config_.jump_speed;
        state.grounded = false;
    }

    if (input.dash_pressed && !state.grounded && state.air_dash_available) {
        const Vec3 dash_direction = NormalizedMoveDirection(input.move, state.last_facing);
        state.velocity.x = dash_direction.x * config_.dash_speed;
        state.velocity.y = 0.0f;
        state.velocity.z = dash_direction.z * config_.dash_speed;
        state.dash_time_remaining = config_.dash_duration;
        state.air_dash_available = false;
    }

    if (state.dash_time_remaining > 0.0f) {
        state.dash_time_remaining -= delta_seconds;
    } else {
        const float target_x = has_move_input ? state.last_facing.x * config_.run_speed : 0.0f;
        const float target_z = has_move_input ? state.last_facing.z * config_.run_speed : 0.0f;
        const float horizontal_accel = state.grounded
            ? (has_move_input ? config_.ground_acceleration : config_.ground_friction)
            : config_.air_acceleration;

        state.velocity.x = MoveToward(state.velocity.x, target_x, horizontal_accel * delta_seconds);
        state.velocity.z = MoveToward(state.velocity.z, target_z, horizontal_accel * delta_seconds);
        state.velocity.y -= config_.gravity * delta_seconds;
    }

    state.position.x += state.velocity.x * delta_seconds;
    state.position.y += state.velocity.y * delta_seconds;
    state.position.z += state.velocity.z * delta_seconds;
}

}  // namespace madeline_cube

