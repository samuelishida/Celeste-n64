#include "gameplay/player/player_controller.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "gameplay/runtime/math.hpp"

namespace madeline_cube {
namespace {

constexpr float kEpsilon = 0.0001f;

float Length(float x, float y) {
    return std::sqrt((x * x) + (y * y));
}

void StartJump(PlayerState& state, const MovementConfig& config, const Vec3& move_input) {
    // If coyote time is active, snap back to the ground Y where coyote was recorded
    if (state.coyote_time_remaining > 0.0f) {
        state.position.y = state.contact.coyote_height;
    }
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

void StartDashJump(PlayerState& state, const MovementConfig& config, const Vec3& move_input, const PlayerController::StepContext& context) {
    state.velocity.y = config.dash_jump_speed;
    state.hold_jump_speed = config.dash_jump_hold_speed;
    state.hold_jump_time_remaining = config.dash_jump_hold_time;
    state.coyote_time_remaining = 0.0f;
    state.auto_jump = false;
    state.air_dash_available = true;

    // Dash-jump momentum conservation: preserve dash horizontal velocity
    state.velocity.x = context.dash_momentum.x;
    state.velocity.z = context.dash_momentum.z;

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
        input.ConsumeJumpPress();  // claim the press that this jump consumed
        StartJump(state, config, move_input);
        return;
    }

    // OG hold-jump: while hold time remains and jump is held (or auto_jump),
    // maintain velocity at hold_speed so the player keeps rising.
    if (state.hold_jump_time_remaining > 0.0f && (input.jump_held || state.auto_jump)) {
        if (state.velocity.y < state.hold_jump_speed) {
            state.velocity.y = state.hold_jump_speed;
        }
        state.hold_jump_time_remaining -= delta_seconds;
        return;
    }

    // After hold time expires (or jump released): apply gravity
    // OG model: single gravity value (600) with 0.5x multiplier when
    // jump held near apex. No separate rise/apex/fall bands.
    float gravity_mult = 1.0f;
    if ((input.jump_held || state.auto_jump) && std::fabs(state.velocity.y) < config.half_gravity_threshold) {
        gravity_mult = 0.5f;
    } else {
        state.auto_jump = false;
    }
    if (!input.jump_held) {
        state.auto_jump = false;
    }

    state.velocity.y = Approach(
        state.velocity.y,
        config.max_fall_speed,
        config.gravity * gravity_mult * delta_seconds
    );
}

// Skid: running fast and pushing the opposite direction. Preserves momentum,
// decelerates at skid_start_accel then skid_accel, and allows a skid jump.
void EnterSkid(PlayerState& state, const MovementConfig& config) {
    state.skidding = true;
    state.locomotion_state = LocomotionState::Run;
    // Preserve momentum: keep current velocity, only decelerate.
    (void)config;
}

void UpdateSkid(
    PlayerState& state,
    const MovementConfig& config,
    const Vec3& move_input,
    bool has_move_input,
    float delta_seconds
) {
    const Vec3 horizontal_velocity = {state.velocity.x, 0.0f, state.velocity.z};
    const float speed_xz = LengthXZ(horizontal_velocity);

    // Exit when slow enough or input agrees with velocity direction.
    if (speed_xz <= config.end_skid_speed) {
        state.skidding = false;
        return;
    }
    if (has_move_input) {
        const float dot = DotXZ(NormalizeXZ(horizontal_velocity, state.target_facing), move_input);
        if (dot > 0.0f) {
            state.skidding = false;
            return;
        }
    }

    // Decelerate: start fast, then ease into skid_accel.
    const float accel = speed_xz > config.run_speed
        ? config.skid_start_accel
        : config.skid_accel;
    const Vec3 target = {0.0f, state.velocity.y, 0.0f};
    state.velocity = ApproachXZ(state.velocity, target, accel * delta_seconds);
}

// Slope speed multiplier (§42): scale ground speed by the ground normal.
// Flat ground ({0,1,0}) -> 1.0; downhill (moving with slope) -> up to
// slope_downhill_mult; uphill -> down to slope_uphill_mult. Clamped.
float SlopeSpeedMultiplier(
    const Vec3& ground_normal,
    const Vec3& move_dir,
    const MovementConfig& config
) {
    // Guard against near-vertical normals (walls) — only apply on real floors.
    if (ground_normal.y < 0.5f) {
        return 1.0f;
    }
    const float dot = DotXZ(ground_normal, move_dir);
    // dot > 0 means moving downhill (normal leans forward), dot < 0 uphill.
    if (dot >= 0.0f) {
        return 1.0f + (dot * (config.slope_downhill_mult - 1.0f));
    }
    return 1.0f + (dot * (1.0f - config.slope_uphill_mult));
}

// Ledge assist (§43): when the input direction has no floor ahead, steer the
// desired movement toward the nearest valid floor direction within ±search_deg.
// Returns the (possibly steered) move direction. No input -> no assist.
Vec3 LedgeAssist(
    const Room& room,
    const Vec3& position,
    const Vec3& move_dir,
    float search_deg
) {
    constexpr float kProbeDist = 20.0f;
    constexpr float kPi = 3.14159265358979323846f;
    constexpr float kDegToRad = kPi / 180.0f;

    // Sample floor directly ahead.
    const Vec3 ahead = {
        position.x + move_dir.x * kProbeDist,
        position.y,
        position.z + move_dir.z * kProbeDist,
    };
    const GroundHit ahead_hit = QueryFloorSource(room, ahead, kProbeDist);
    if (ahead_hit.hit) {
        return move_dir;  // floor ahead, no assist needed
    }

    // Search ±search_deg for a valid floor.
    const float base_angle = AngleXZ(move_dir);
    const float max_delta = search_deg * kDegToRad;
    constexpr int kSamples = 8;
    for (int i = 1; i <= kSamples; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(kSamples);
        for (int sign = -1; sign <= 1; sign += 2) {
            const float angle = base_angle + (sign * max_delta * t);
            const Vec3 dir = DirectionFromAngle(angle);
            const Vec3 sample = {
                position.x + dir.x * kProbeDist,
                position.y,
                position.z + dir.z * kProbeDist,
            };
            const GroundHit hit = QueryFloorSource(room, sample, kProbeDist);
            if (hit.hit) {
                return dir;
            }
        }
    }
    return move_dir;  // no floor found, no steering
}

}  // namespace

PlayerController::PlayerController(MovementConfig config) : config_(config), profile_(ToProfile(config)) {}

void PlayerController::Step(
    PlayerState& state,
    const PlayerInput& input,
    const Vec3& camera_forward,
    float delta_seconds,
    const Room* room
) const {
    if (delta_seconds <= 0.0f) {
        return;
    }

    StepContext context = TimerInputPhase(state, input, camera_forward, delta_seconds);
    StatePhase(state, input, context, delta_seconds, room);
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
    // Don't update facing from move input while on a wall;
    // climbing uses vertical input but should keep the player facing into the wall.
    if (context.has_move_input && state.movement_state != PlayerMovementState::Climbing) {
        state.last_facing = context.move_input;
    }

    if (state.grounded) {
        state.coyote_time_remaining = profile_.coyote_time;
        state.contact.coyote_height = state.position.y;  // Record Y for coyote-jump snap
        state.wall_grab_time_remaining = config_.wall_grab_time;
        context.dash_momentum = {};
        if (state.dash_reset_cooldown_remaining <= 0.0f) {
            state.air_dash_available = true;
        }
        // Stamina refills on landing; clears climb exhaustion
        state.stamina = state.stamina_max;
        state.climb_exhausted = false;
    } else {
        state.coyote_time_remaining = Approach(state.coyote_time_remaining, 0.0f, delta_seconds);
    }

    if (input.jump_pressed) {
        state.jump_buffer_remaining = profile_.jump_buffer_time;
    } else {
        state.jump_buffer_remaining = Approach(state.jump_buffer_remaining, 0.0f, delta_seconds);
    }

    state.dash_cooldown_remaining = Approach(state.dash_cooldown_remaining, 0.0f, delta_seconds);
    state.dash_reset_cooldown_remaining = Approach(state.dash_reset_cooldown_remaining, 0.0f, delta_seconds);
    state.no_dash_jump_remaining = Approach(state.no_dash_jump_remaining, 0.0f, delta_seconds);
    state.wall_jump_cooldown_remaining = Approach(state.wall_jump_cooldown_remaining, 0.0f, delta_seconds);
    state.climb.cooldown_remaining = Approach(state.climb.cooldown_remaining, 0.0f, delta_seconds);
    state.climb.hop_no_move_remaining = Approach(state.climb.hop_no_move_remaining, 0.0f, delta_seconds);
    state.no_move_time_remaining = Approach(state.no_move_time_remaining, 0.0f, delta_seconds);
    state.contact.ground_snap_cooldown_remaining = Approach(
        state.contact.ground_snap_cooldown_remaining,
        0.0f,
        delta_seconds
    );
    state.platform_carry.time_remaining = Approach(state.platform_carry.time_remaining, 0.0f, delta_seconds);

    return context;
}

void PlayerController::StatePhase(
    PlayerState& state,
    const PlayerInput& input,
    StepContext& context,
    float delta_seconds,
    const Room* room
) const {
    const Vec3& move_input = context.move_input;
    const bool has_move_input = context.has_move_input;
    const float raw_input_length = context.raw_input_length;
    if (delta_seconds <= 0.0f) {
        return;
    }

    // Climbing (with OG Celeste64 stamina system)
    if (state.movement_state == PlayerMovementState::Climbing) {
        // Use climb.wall_normal (captured at entry) for facing dot.
        // Also consider the current state.wall_normal if it has data.
        Vec3 climb_normal = state.climb.wall_normal;
        if (LengthXZ(state.wall_normal) > 0.1f) climb_normal = state.wall_normal;
        const float wall_facing_dot = DotXZ(climb_normal,
            {-state.last_facing.x, 0.0f, -state.last_facing.z});

        // Release conditions: let go of grab, stamina exhausted, lost wall, not facing wall
        if (!input.climb_held || state.stamina <= 0.0f ||
            !(state.wall_left || state.wall_right) ||
            wall_facing_dot < 0.5f) {
            state.movement_state = PlayerMovementState::Normal;
            state.locomotion_state = LocomotionState::Fall;
            state.climb.cooldown_remaining = config_.climb_cooldown;
            if (state.stamina <= 0.0f) {
                state.climb_exhausted = true;
            }
            return;
        }

        // Wall jump from climb (claims the jump press so it cannot also
        // trigger a normal jump / buffer this frame).
        if (input.ConsumeJumpPress()) {
            state.jump_buffer_remaining = 0.0f;  // press already claimed
            if (state.stamina >= config_.stamina_jump_cost) {
                state.stamina -= config_.stamina_jump_cost;
                state.movement_state = PlayerMovementState::Normal;
                // Jump away from wall: velocity in wall_normal direction
                const Vec3 fallback_dir = state.wall_left ? Vec3{1.0f,0.0f,0.0f} : Vec3{-1.0f,0.0f,0.0f};
                const Vec3 jump_dir = LengthXZ(state.wall_normal) > 0.1f
                    ? NormalizeXZ(state.wall_normal, fallback_dir)
                    : fallback_dir;
                const bool neutral = !has_move_input;
                const float horizontal = neutral
                    ? profile_.neutral_wall_jump_horizontal_speed
                    : profile_.wall_jump_horizontal_speed;
                state.velocity.x = jump_dir.x * horizontal;
                state.velocity.y = neutral
                    ? profile_.neutral_wall_jump_vertical_speed
                    : profile_.wall_jump_vertical_speed;
                state.velocity.z = jump_dir.z * horizontal;
                state.target_facing = jump_dir;
                state.last_facing = jump_dir;
                state.locomotion_state = LocomotionState::Jump;
                state.hold_jump_speed = config_.jump_speed;
                state.hold_jump_time_remaining = config_.jump_hold_time;
                state.climb.cooldown_remaining = config_.climb_cooldown;
                return;
            }
        }

        // Climb hop over ledge (up input at top of wall)
        // TODO: detect ledge geometry and trigger hop when climbing above ledge

        // Stamina drain while climbing
        const float vertical_input = input.move.y;
        if (vertical_input > 0.0f) {
            // Climbing up: hold drain + up drain
            state.stamina -= (config_.stamina_hold_drain + config_.stamina_up_drain) * delta_seconds;
        } else {
            // Holding (idle or sliding down): hold drain only
            state.stamina -= config_.stamina_hold_drain * delta_seconds;
        }
        if (state.stamina <= 0.0f) {
            state.stamina = 0.0f;
            state.climb_exhausted = true;
            state.movement_state = PlayerMovementState::Normal;
            state.locomotion_state = LocomotionState::Fall;
            state.climb.cooldown_remaining = config_.climb_cooldown;
            return;
        }

        // Climb movement: move along wall surface
        state.velocity.x = 0.0f;
        state.velocity.z = 0.0f;
        state.velocity.y = vertical_input * profile_.climb_speed;
        state.locomotion_state = LocomotionState::Climb;
        return;
    }

    // Climb entry: airborne + wall contact + facing wall + climb held + not exhausted + stamina > 0
    {
        // Compute facing dot: how much the player faces into the wall.
        // When wall_normal is not set, derive from wall_left/wall_right.
        Vec3 entry_normal = state.wall_normal;
        if (LengthXZ(entry_normal) < 0.1f) {
            if (state.wall_left)  entry_normal = {1.0f, 0.0f, 0.0f};
            if (state.wall_right) entry_normal = {-1.0f, 0.0f, 0.0f};
        }
        const float wall_facing_dot = DotXZ(entry_normal,
            {-state.last_facing.x, 0.0f, -state.last_facing.z});
        const bool facing_wall = wall_facing_dot >= 0.5f;

        if (input.climb_held &&
            state.climb.cooldown_remaining <= 0.0f &&
            !state.climb_exhausted &&
            state.stamina > 0.0f &&
            !state.grounded &&
            (state.wall_left || state.wall_right) &&
            state.wall_climbable &&
            facing_wall) {
            state.movement_state = PlayerMovementState::Climbing;
            state.locomotion_state = LocomotionState::Climb;
            state.climb.wall_normal = entry_normal;
            state.velocity = {};
            return;
        }
    }

    // Wall slide: grab a vertical wall while airborne by holding the Climb
    // button (Z/L/R) — NOT the jump button. Per spec §15-16 the grab/climb
    // action is bound to Climb; this forgiving fallback lets the player latch
    // and slide even when the wall lacks MAT_CLIMBABLE (which the climb-entry
    // path below requires).
    const bool can_wall_grab = !state.grounded &&
                               (state.wall_left || state.wall_right) &&
                               state.wall_grab_time_remaining > 0.0f &&
                               state.wall_jump_cooldown_remaining <= 0.0f;
    if (can_wall_grab && input.climb_held) {
        state.wall_grabbing = true;
    } else if (!input.climb_held || state.grounded) {
        state.wall_grabbing = false;
    }

    if (state.wall_grabbing) {
        state.wall_grab_time_remaining = Approach(state.wall_grab_time_remaining, 0.0f, delta_seconds);
        state.velocity.y = -config_.wall_slide_speed;
        state.velocity.x = 0.0f;
        state.velocity.z = 0.0f;

        if (input.ConsumeJumpPress()) {
            state.jump_buffer_remaining = 0.0f;  // press already claimed
            state.wall_grabbing = false;
            state.wall_jump_cooldown_remaining = config_.wall_jump_cooldown;
            // Jump away from wall using wall_normal direction
            const Vec3 fallback_dir = state.wall_left ? Vec3{1.0f,0.0f,0.0f} : Vec3{-1.0f,0.0f,0.0f};
            const Vec3 jump_dir = LengthXZ(state.wall_normal) > 0.1f
                ? NormalizeXZ(state.wall_normal, fallback_dir)
                : fallback_dir;
            state.target_facing = jump_dir;
            state.last_facing = jump_dir;
            state.velocity.x = jump_dir.x * config_.wall_jump_speed_x;
            state.velocity.y = config_.wall_jump_speed_y;
            state.velocity.z = jump_dir.z * config_.wall_jump_speed_x;
            state.hold_jump_speed = config_.jump_speed;
            state.hold_jump_time_remaining = config_.jump_hold_time;
        }
    }

    // Directional wall jump (without grab)
    if (!state.wall_grabbing &&
        !state.grounded &&
        input.ConsumeJumpPress() &&
        (state.wall_left || state.wall_right)) {
        state.jump_buffer_remaining = 0.0f;  // press already claimed
        const Vec3 fallback_dir = state.wall_left ? Vec3{1.0f,0.0f,0.0f} : Vec3{-1.0f,0.0f,0.0f};
        const Vec3 jump_dir = LengthXZ(state.wall_normal) > 0.1f
            ? NormalizeXZ(state.wall_normal, fallback_dir)
            : fallback_dir;
        state.target_facing = jump_dir;
        state.last_facing = jump_dir;
        state.velocity.x = jump_dir.x * config_.wall_jump_speed_x;
        state.velocity.y = config_.wall_jump_speed_y;
        state.velocity.z = jump_dir.z * config_.wall_jump_speed_x;
        state.hold_jump_speed = config_.jump_speed;
        state.hold_jump_time_remaining = config_.jump_hold_time;
        state.wall_jump_cooldown_remaining = config_.wall_jump_cooldown;
    }

    if (!state.wall_grabbing &&
        input.ConsumeDashPress() &&
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
        context.dash_momentum = {};
        state.velocity = {};
    }

    if (!state.wall_grabbing) {
        if (state.movement_state == PlayerMovementState::Dashing) {
            state.dash_time_remaining -= delta_seconds;
            if (state.dash_hitstop_remaining > 0.0f) {
                state.dash_hitstop_remaining = Approach(state.dash_hitstop_remaining, 0.0f, delta_seconds);
                state.velocity = {};
                return;
            }
            state.dash_active_remaining = Approach(state.dash_active_remaining, 0.0f, delta_seconds);
            state.velocity.x = state.target_facing.x * profile_.dash_speed;
            state.velocity.y = 0.0f;
            state.velocity.z = state.target_facing.z * profile_.dash_speed;
            // Store dash momentum for conservation into jump
            context.dash_momentum.x = state.velocity.x;
            context.dash_momentum.z = state.velocity.z;
            if (state.dash_time_remaining <= 0.0f) {
                if (!state.grounded) {
                    state.velocity.x *= config_.dash_end_speed_multiplier;
                    state.velocity.z *= config_.dash_end_speed_multiplier;
                }
                state.movement_state = PlayerMovementState::Normal;
                state.dash_cooldown_remaining = config_.dash_cooldown;
            } else {
                if (state.dashed_on_ground &&
                    state.coyote_time_remaining > 0.0f &&
                    state.no_dash_jump_remaining <= 0.0f &&
                    input.ConsumeJumpPress()) {
                    state.jump_buffer_remaining = 0.0f;  // press already claimed
                    state.movement_state = PlayerMovementState::Normal;
                    StartDashJump(state, config_, move_input, context);
                }
            }
        }

        if (state.movement_state == PlayerMovementState::Normal) {
            const Vec3 horizontal_velocity = {state.velocity.x, 0.0f, state.velocity.z};
            const float speed_xz = LengthXZ(horizontal_velocity);
            const float desired_speed = profile_.run_max_speed * AnalogMagnitude(raw_input_length);
            const Vec3 target_velocity = has_move_input
                ? Vec3{move_input.x * desired_speed, state.velocity.y, move_input.z * desired_speed}
                : Vec3{0.0f, state.velocity.y, 0.0f};

            if (state.grounded) {
                // Ledge assist (§43): steer input toward a valid floor ahead.
                Vec3 grounded_move = move_input;
                if (has_move_input && room != nullptr) {
                    grounded_move = LedgeAssist(*room, state.position, move_input, 17.0f);
                }

                // Slope speed multiplier (§42): scale desired speed by ground normal.
                const float slope_mult = has_move_input
                    ? SlopeSpeedMultiplier(state.contact.ground_normal, grounded_move, config_)
                    : 1.0f;
                const float grounded_desired_speed = desired_speed * slope_mult;

                // Skid: running fast and pushing opposite direction.
                if (has_move_input && speed_xz > profile_.rotate_threshold) {
                    const float input_dot = DotXZ(
                        NormalizeXZ(horizontal_velocity, state.target_facing),
                        grounded_move);
                    if (input_dot <= config_.skid_dot_threshold) {
                        if (!state.skidding) {
                            EnterSkid(state, config_);
                        }
                    } else if (state.skidding) {
                        state.skidding = false;
                    }
                } else if (state.skidding) {
                    state.skidding = false;
                }

                if (state.skidding) {
                    UpdateSkid(state, config_, grounded_move, has_move_input, delta_seconds);
                } else if (has_move_input && speed_xz > profile_.rotate_threshold) {
                    // OG: rotation-based turning when above rotate threshold
                    state.target_facing = RotateTowardXZ(state.target_facing, grounded_move, profile_.rotate_speed * delta_seconds);
                    const float accel = profile_.ground_acceleration;
                    const Vec3 rotated_target = {
                        state.target_facing.x * grounded_desired_speed,
                        state.velocity.y,
                        state.target_facing.z * grounded_desired_speed,
                    };
                    state.velocity = ApproachXZ(state.velocity, rotated_target, accel * delta_seconds);
                } else {
                    // Ice (§): low-friction ground — decelerate much slower.
                    const float ice_mult = state.on_ice ? config_.ice_friction_mult : 1.0f;
                    const float accel = has_move_input
                        ? profile_.ground_acceleration
                        : profile_.ground_deceleration * ice_mult;
                    const Vec3 grounded_target = has_move_input
                        ? Vec3{grounded_move.x * grounded_desired_speed, state.velocity.y, grounded_move.z * grounded_desired_speed}
                        : target_velocity;
                    state.velocity = ApproachXZ(state.velocity, grounded_target, accel * delta_seconds);
                }
                state.locomotion_state = has_move_input || speed_xz > kEpsilon
                    ? LocomotionState::Run
                    : LocomotionState::Idle;
                if (has_move_input) {
                    state.target_facing = grounded_move;
                    state.facing = grounded_move;
                }
            } else {
                // Air movement: OG scales accel by dot product of input vs velocity direction
                state.skidding = false;
                const float input_dot = has_move_input
                    ? DotXZ(NormalizeXZ(horizontal_velocity, state.target_facing), move_input)
                    : 1.0f;
                float accel_mult = 1.0f;
                if (input_dot < 0.0f) {
                    // Reversing direction: more accel (air_turn)
                    accel_mult = config_.air_accel_mult_min + (config_.air_accel_mult_max - config_.air_accel_mult_min) * (1.0f + input_dot);
                }
                float accel;
                if (speed_xz > profile_.run_max_speed) {
                    // Above max: past_max_decel (slower acceleration to preserve momentum)
                    accel = profile_.past_max_decel * accel_mult;
                } else {
                    accel = profile_.air_acceleration * accel_mult;
                }
                if (has_move_input) {
                    // OG: rotation-based turning in air too
                    if (speed_xz > profile_.rotate_threshold) {
                        state.target_facing = RotateTowardXZ(state.target_facing, move_input, profile_.rotate_speed * delta_seconds);
                        const Vec3 rotated_target = {
                            state.target_facing.x * desired_speed,
                            state.velocity.y,
                            state.target_facing.z * desired_speed,
                        };
                        state.velocity = ApproachXZ(state.velocity, rotated_target, accel * delta_seconds);
                    } else {
                        state.velocity = ApproachXZ(state.velocity, target_velocity, accel * delta_seconds);
                    }
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
