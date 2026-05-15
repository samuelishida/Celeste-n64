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
    const float len = Length(move.x, move.y);
    if (len <= 0.0001f) {
        return fallback;
    }
    return {move.x / len, 0.0f, move.y / len};
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

    // --- Grounding and coyote time ---
    if (state.grounded) {
        state.air_dash_available = true;
        state.coyote_time_remaining = config_.coyote_time;
        state.wall_grab_time_remaining = config_.wall_grab_time;
    } else {
        state.coyote_time_remaining -= delta_seconds;
        if (state.coyote_time_remaining < 0.0f) {
            state.coyote_time_remaining = 0.0f;
        }
    }

    // --- Jump buffering ---
    if (input.jump_pressed) {
        state.jump_buffer_remaining = config_.jump_buffer_time;
    } else {
        state.jump_buffer_remaining -= delta_seconds;
        if (state.jump_buffer_remaining < 0.0f) {
            state.jump_buffer_remaining = 0.0f;
        }
    }

    // --- Wall grab logic ---
    bool can_wall_grab = !state.grounded && state.velocity.y < 0.0f &&
                         (state.wall_left || state.wall_right) &&
                         state.wall_grab_time_remaining > 0.0f &&
                         state.wall_jump_cooldown_remaining <= 0.0f;

    if (can_wall_grab && input.jump_held) {
        state.wall_grabbing = true;
    } else if (!input.jump_held) {
        state.wall_grabbing = false;
    }

    if (state.wall_grabbing) {
        state.wall_grab_time_remaining -= delta_seconds;
        state.velocity.y = -config_.wall_slide_speed;
        state.velocity.x = 0.0f;
        state.velocity.z = 0.0f;
    }

    // --- Jump execution ---
    bool can_jump = state.grounded || state.coyote_time_remaining > 0.0f || state.wall_grabbing;
    if (state.jump_buffer_remaining > 0.0f && can_jump) {
        state.jump_buffer_remaining = 0.0f;
        state.coyote_time_remaining = 0.0f;

        if (state.wall_grabbing) {
            // Wall jump
            state.wall_grabbing = false;
            state.wall_jump_cooldown_remaining = config_.wall_jump_cooldown;
            const float dir_x = state.wall_left ? 1.0f : -1.0f;
            state.velocity.x = dir_x * config_.wall_jump_speed_x;
            state.velocity.y = config_.wall_jump_speed_y;
            state.velocity.z = 0.0f;
            state.last_facing = {dir_x, 0.0f, 0.0f};
        } else {
            // Normal jump
            state.velocity.y = config_.jump_speed;
            state.grounded = false;
            state.jump_held_active = true;
        }
    }

    // --- Variable jump height ---
    if (state.jump_held_active && !input.jump_held && state.velocity.y > 0.0f) {
        state.velocity.y *= config_.variable_jump_cut;
        state.jump_held_active = false;
    }
    if (state.grounded) {
        state.jump_held_active = false;
    }

    // --- Dash ---
    if (input.dash_pressed && !state.grounded && state.air_dash_available && !state.wall_grabbing) {
        const Vec3 dash_direction = NormalizedMoveDirection(input.move, state.last_facing);
        state.velocity.x = dash_direction.x * config_.dash_speed;
        state.velocity.y = 0.0f;
        state.velocity.z = dash_direction.z * config_.dash_speed;
        state.dash_time_remaining = config_.dash_duration;
        state.air_dash_available = false;
        state.wall_grabbing = false;
    }

    // --- Cooldowns ---
    if (state.dash_time_remaining > 0.0f) {
        state.dash_time_remaining -= delta_seconds;
    }
    if (state.wall_jump_cooldown_remaining > 0.0f) {
        state.wall_jump_cooldown_remaining -= delta_seconds;
    }

    // --- Horizontal movement (only when not dashing or wall-grabbing) ---
    if (state.dash_time_remaining <= 0.0f && !state.wall_grabbing) {
        const float target_x = has_move_input ? state.last_facing.x * config_.run_speed : 0.0f;
        const float target_z = has_move_input ? state.last_facing.z * config_.run_speed : 0.0f;
        const float horizontal_accel = state.grounded
            ? (has_move_input ? config_.ground_acceleration : config_.ground_friction)
            : config_.air_acceleration;

        state.velocity.x = MoveToward(state.velocity.x, target_x, horizontal_accel * delta_seconds);
        state.velocity.z = MoveToward(state.velocity.z, target_z, horizontal_accel * delta_seconds);
    }

    // --- Gravity ---
    if (!state.grounded && !state.wall_grabbing) {
        state.velocity.y -= config_.gravity * delta_seconds;
    }

    // --- Integration ---
    state.position.x += state.velocity.x * delta_seconds;
    state.position.y += state.velocity.y * delta_seconds;
    state.position.z += state.velocity.z * delta_seconds;
}

}  // namespace madeline_cube

