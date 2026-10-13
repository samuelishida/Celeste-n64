#include "gameplay/player/player_controller.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#ifdef N64
#include <libdragon.h>
#endif

#include "gameplay/physics/coll_mesh.hpp"
#include "gameplay/runtime/math.hpp"
#include "gameplay/world/hazard_query.hpp"

namespace madeline_cube {
namespace {

constexpr float kEpsilon = 0.0001f;

// Shortest signed angle difference in (-pi, pi] (OG Calc.AngleLerp parity for
// the corner-ease facing lerp).
inline float ShortestAngle(float a) {
    constexpr float kTau = 6.28318530717958647692f;
    a = std::fmod(a + 3.14159265358979323846f, kTau);
    if (a < 0.0f) a += kTau;
    return a - 3.14159265358979323846f;
}

// og-mover-overhaul review C7: the no-op static_assert(false-true) parity
// notice was removed — the real guarantee is MovementConfig::player_half_height
// staying equal to MoveServiceConfig::half_height, both defaulted to 11.25.

float Length(float x, float y) {
    return std::sqrt((x * x) + (y * y));
}

MoveServiceConfig MakePlayerMoveConfig(const MovementConfig& config) {
    MoveServiceConfig mover{};
    mover.wall_radius = config.player_wall_radius;
    mover.climb_wall_radius = config.climb_pushout_dist;
    mover.half_height = config.player_half_height;
    mover.ground_snap_cooldown = config.ground_snap_cooldown;
    return mover;
}

void AddStoredPlatformVelocity(PlayerState& state) {
    if (state.platform_carry.time_remaining <= 0.0f) {
        return;
    }

    // OG AddPlatformVelocity: cap upward carry at 180, discard downward carry,
    // and cap horizontal carry magnitude at 300 before adding it to launch.
    Vec3 carry = state.platform_carry.stored_velocity;
    carry.y = Clamp(carry.y, 0.0f, 180.0f);
    const float horizontal_speed = LengthXZ(carry);
    if (horizontal_speed > 300.0f) {
        const float scale = 300.0f / horizontal_speed;
        carry.x *= scale;
        carry.z *= scale;
    }

    state.velocity.x += carry.x;
    state.velocity.y += carry.y;
    state.velocity.z += carry.z;
    state.platform_carry = {};
}

// OG Player.Update subtracts active timers before dispatching the state and
// lets a positive timer cross zero for that tick. Keep the same boundary
// semantics instead of clamping with Approach().
void TickTimer(float& remaining, float delta_seconds) {
    if (remaining > 0.0f) remaining -= delta_seconds;
}

void SetDashVelocity(PlayerState& state, const MovementConfig& config) {
    const Vec3 direction = state.target_facing;
    if (state.dashed_on_ground) {
        state.velocity = {
            direction.x * config.dash_speed,
            0.0f,
            direction.z * config.dash_speed,
        };
        return;
    }

    // OG Player.SetDashSpeed: airborne dash aims slightly upward, then
    // normalizes the full (horizontal direction, 0.4) vector to DashSpeed.
    const float scale = config.dash_speed / std::sqrt(physics::Len2(direction) + 0.16f);
    state.velocity = {
        direction.x * scale,
        0.4f * scale,
        direction.z * scale,
    };
}

// ── OG climb probe helpers (climb-parity-3 Inc 1) ────────────────────
//
// OG Z-up → demake +Y-up: position is the body CENTER; feet = center.y -
// player_half_height. OG SolidWaistTestPos = feet + 3 (climb_waist_offset),
// probe radius = ClimbCheckDist (4), prefer = -facing.
//
// Mirrors OG ClimbCheckAt(Vec3 offset): one plane-based query family drives
// grip, ledge, and corner decisions. `y_extra` is the vertical offset ABOVE
// the waist probe (OG's UnitZ extra) — 0 = grip check, 1 = ledge check.

// Waist probe center for a body-center position.
inline Vec3 ClimbWaistPoint(const PlayerState& state, const MovementConfig& config, float y_extra = 0.0f) {
    const float feet_y = state.position.y - config.player_half_height;
    return {
        state.position.x,
        feet_y + config.climb_waist_offset + y_extra,
        state.position.z,
    };
}

// OG ClimbCheckAt: single-query wall test at waist + y_extra. Returns the
// plane-based hit (prefer-ranked by -facing) or a miss. Falls back to a
// miss for room==nullptr fixtures (the flag fallback lives in the callers).
inline WallHit ClimbCheckAt(const PlayerState& state, const MovementConfig& config,
                            const Room* room, float y_extra = 0.0f,
                            const Vec3& relative_input = {}) {
    if (room == nullptr) return WallHit{};
    const Vec3 into_wall = {-state.target_facing.x, 0.0f, -state.target_facing.z};
    WallHit hit = QueryClimbWall(*room, ClimbWaistPoint(state, config, y_extra),
                               config.climb_check_dist, into_wall);
    if (hit.hit && LengthXZ(relative_input) > kEpsilon &&
        DotXZ(NormalizeXZ(hit.plane_normal), relative_input) > -0.5f)
        return {};
    return hit;
}

// ── OG corner mechanics (climb-parity-3 Inc 3; Player.cs:1567-1630) ────
// Z-up→Y-up conversion: OG `.XY()` = demake XZ, OG UnitZ = demake +Y.
// All adopts re-anchor the CENTER to the new wall (same form as entry)
// and reset grip identity + facing INTO the new wall.

// Adopt a ray-hit as the new gripped wall: re-anchor + identity swap.
inline void AdoptCornerWall(PlayerState& state, const MovementConfig& config,
                            const WallHit& hit) {
    const Vec3 n = hit.plane_normal;
    state.climb.wall_normal = n;
    state.climb.grip_face_id = hit.face_id;
    state.climb.wall_surface_id = hit.owner_id;
    state.position = {
        hit.point.x + n.x * config.climb_pushout_dist,
        hit.point.y + n.y * config.climb_pushout_dist +
            (config.player_half_height - config.climb_waist_offset),
        hit.point.z + n.z * config.climb_pushout_dist,
    };
    state.target_facing = {-n.x, 0.0f, -n.z};
    state.last_facing = state.target_facing;

}

// Source Utils.UpwardPerpendicularNormal, adapted by a proper axis rotation.
Vec3 ClimbWallUp(const Vec3& normal) {
    const Vec3 n = physics::Norm(normal);
    const Vec3 arbitrary = std::fabs(n.x) > std::fabs(n.z)
        ? Vec3{0.0f, 0.0f, -1.0f} : Vec3{1.0f, 0.0f, 0.0f};
    Vec3 up = physics::Norm(physics::Cross(n, arbitrary));
    if (up.y < 0.0f) up = physics::Scale(up, -1.0f);
    return up;
}

Vec3 ClimbWallRight(const Vec3& normal) {
    // Rodrigues rotation of wallUp around normal by -pi/2.
    return physics::Scale(physics::Cross(physics::Norm(normal), ClimbWallUp(normal)), -1.0f);
}

// OG normalizes the lateral climb tangent and gates on a minimum magnitude.
// Without this the corner probe reach (and its sideways origin offset) scales
// with analog-stick deflection, so a light tap and a full push behave
// differently and the result is frame/analog-range dependent.
constexpr float kCornerSideMinMagnitude = 0.1f;
Vec3 CornerSideDirection(const Vec3& side_input) {
    const float len = std::sqrt(physics::Len2(side_input));
    if (len < kCornerSideMinMagnitude) return {};
    return physics::Scale(side_input, 1.0f / len);
}

bool InnerCornerSideRay(PlayerState& state, const MovementConfig& config,
                        const Room* room, const Vec3& /*normal*/,
                        const Vec3& side_input, WallHit* adopted = nullptr) {
    if (!room) return false;
    const Vec3 side_dir = CornerSideDirection(side_input);
    if (physics::Len2(side_dir) == 0.0f) return false;
    WallHit hit{};
    if (!QueryWallRay(*room, ClimbWaistPoint(state, config), side_dir,
                      config.climb_check_dist, &hit)) return false;
    if (adopted) *adopted = hit;
    AdoptCornerWall(state, config, hit);
    return true;
}

bool OuterCornerRay(PlayerState& state, const MovementConfig& config,
                    const Room* room, const Vec3& /*normal*/,
                    const Vec3& side_input, const WallHit& /*gripped*/,
                    WallHit* adopted = nullptr) {
    if (!room) return false;
    const Vec3 side_dir = CornerSideDirection(side_input);
    if (physics::Len2(side_dir) == 0.0f) return false;
    const Vec3 forward = state.target_facing;
    const Vec3 origin = physics::Add(ClimbWaistPoint(state, config),
        physics::Add(physics::Scale(forward, config.climb_check_dist + 1.0f), side_dir));
    WallHit hit{};
    if (!QueryWallRay(*room, origin, physics::Scale(side_dir, -1.0f),
                      config.climb_check_dist * 2.0f, &hit) ||
        std::fabs(hit.plane_normal.y) >= 0.35f) return false;
    if (adopted) *adopted = hit;
    AdoptCornerWall(state, config, hit);
    return true;
}

Vec3 AvoidGroundLedge(const PlayerState& state, const MovementConfig& config,
                      const Room* room, const Vec3& move_input) {
    if (room == nullptr || config.ledge_sweep_dist <= 0.0f ||
        LengthXZ(move_input) <= kEpsilon) {
        return move_input;
    }

    // OG Player.cs:1032-1082 probes from the feet anchor. Demake stores body
    // center, so translate to feet before applying the source offsets.
    const float distance = config.ledge_sweep_dist;
    const float feet_y = state.position.y - config.player_half_height;
    const Vec3 feet_origin = {state.position.x, feet_y, state.position.z};
    const auto floor_ahead = [&](const Vec3& direction) {
        const Vec3 origin = {
            feet_origin.x + direction.x * distance,
            feet_origin.y + distance,
            feet_origin.z + direction.z * distance,
        };
        return RaycastRoomSource(*room, origin, {0.0f, -1.0f, 0.0f},
                                 distance * 2.0f, BackfacePolicy::Ignore).hit;
    };

    // A real floor ahead or a solid wall directly ahead makes this ordinary
    // movement, not a ledge corner. Keep the shared input snapshot unchanged.
    if (floor_ahead(move_input) ||
        RaycastRoomSource(*room,
                          {feet_origin.x, feet_y + distance, feet_origin.z},
                          move_input, distance, BackfacePolicy::Ignore).hit) {
        return move_input;
    }

    constexpr float kInitialAngle = 0.3f;
    constexpr float kAngleStep = 0.1f;
    constexpr int kMaxSweepSteps = 10;
    const float input_angle = AngleXZ(move_input);
    const auto direction_at = [](float angle) {
        return Vec3{std::cos(angle), 0.0f, std::sin(angle)};
    };

    Vec3 left = direction_at(input_angle + kInitialAngle);
    Vec3 right = direction_at(input_angle - kInitialAngle);
    int count = 0;
    if (floor_ahead(left)) {
        while (floor_ahead(left) && count++ < kMaxSweepSteps) {
            left = direction_at(AngleXZ(left) - kAngleStep);
        }
        return direction_at(AngleXZ(left) + kAngleStep);
    }
    if (floor_ahead(right)) {
        while (floor_ahead(right) && count++ < kMaxSweepSteps) {
            right = direction_at(AngleXZ(right) + kAngleStep);
        }
        return direction_at(AngleXZ(right) - kAngleStep);
    }
    return move_input;
}

void StartJump(PlayerState& state, const MovementConfig& config, const Vec3& move_input) {
    // OG Jump always restores Position.Z to the recorded coyote height. This
    // helper is accepted only while that source coyote window is live.
    state.position.y = state.contact.coyote_height;
    state.velocity.y = config.jump_speed;
    state.hold_jump_speed = config.jump_speed;
    state.hold_jump_time_remaining = config.jump_hold_time;

    state.coyote_time_remaining = 0.0f;
    state.auto_jump = false;
    state.model_scale = {0.6f, 1.4f, 0.6f};
    // OG CancelGroundSnap parity (Player.cs:742): a takeoff must never be
    // eaten by the 5-unit ground snap on the next ticks.
    state.contact.ground_snap_cooldown_remaining = config.ground_snap_cooldown;
    AddStoredPlatformVelocity(state);

    if (LengthXZ(move_input) > kEpsilon) {
        state.target_facing = NormalizeXZ(move_input, state.target_facing);
        state.last_facing = state.target_facing;
        state.velocity.x += state.target_facing.x * config.jump_xy_boost;
        state.velocity.z += state.target_facing.z * config.jump_xy_boost;
    }
}

void StartDashJump(PlayerState& state, const MovementConfig& config, const Vec3& move_input, const PlayerController::StepContext& context) {
    // OG DashJump restores coyoteZ before applying its launch; this matters
    // when the ground dash has carried Madeline just beyond a ledge.
    state.position.y = state.contact.coyote_height;
    state.velocity.y = config.dash_jump_speed;
    state.hold_jump_speed = config.dash_jump_hold_speed;
    state.hold_jump_time_remaining = config.dash_jump_hold_time;
    state.coyote_time_remaining = 0.0f;
    state.auto_jump = false;
    state.model_scale = {0.6f, 1.4f, 0.6f};
    state.dashes = std::max(state.dashes, 1);
    state.contact.ground_snap_cooldown_remaining = config.ground_snap_cooldown;  // OG CancelGroundSnap

    // Dash-jump momentum conservation: preserve dash horizontal velocity
    state.velocity.x = context.dash_momentum.x;
    state.velocity.z = context.dash_momentum.z;

    if (LengthXZ(move_input) > kEpsilon) {
        state.target_facing = NormalizeXZ(move_input, state.target_facing);
        state.last_facing = state.target_facing;
        state.velocity.x += state.target_facing.x * config.dash_jump_xy_boost;
        state.velocity.z += state.target_facing.z * config.dash_jump_xy_boost;
    }
    AddStoredPlatformVelocity(state);
}

// Shared by normal directional WallJumpCheck and the climb-state jump exit.
// OG WallJump sets the outward velocity, consumes stored platform motion, and
// cancels ground snap; climb input does not select a different launch profile.
void StartWallJump(PlayerState& state, const MovementConfig& config,
                   const Vec3& jump_direction) {
    const Vec3 direction = jump_direction;
    state.target_facing = direction;
    state.last_facing = direction;
    state.velocity = {
        direction.x * config.wall_jump_speed_x,
        config.wall_jump_speed_y,
        direction.z * config.wall_jump_speed_x,
    };
    state.hold_jump_speed = config.jump_speed;
    state.hold_jump_time_remaining = config.jump_hold_time;
    state.auto_jump = false;
    state.model_scale = {0.6f, 1.4f, 0.6f};
    AddStoredPlatformVelocity(state);
    state.contact.ground_snap_cooldown_remaining = config.ground_snap_cooldown;
    state.locomotion_state = LocomotionState::Jump;
}

void ApplyJumpAndGravity(
    PlayerState& state,
    const PlayerInput& input,
    const Vec3& move_input,
    const MovementConfig& config,
    float delta_seconds
) {
    // OG StNormalUpdate gates Jump on tCoyote alone; grounded is refreshed
    // later by LateUpdate and must not bypass an expired source timer.
    const bool can_jump = state.coyote_time_remaining > 0.0f;
    // Inc 1: the input device owns the 0.10 s press buffer — `input.jump_pressed`
    // is the device-buffered edge (live for the buffer window). Consume it on
    // acceptance so the buffered edge cannot re-fire next frame.
    if (can_jump && input.ConsumeJumpPress()) {
        StartJump(state, config, move_input);
        return;
    }

    // OG hold-jump: while hold time remains and jump is held (or auto_jump),
    // maintain velocity at hold_speed so the player keeps rising.
    if (state.hold_jump_time_remaining > 0.0f && (input.jump_held || state.auto_jump)) {
        if (state.velocity.y < state.hold_jump_speed) {
            state.velocity.y = state.hold_jump_speed;
        }
        return;
    }

    // OG clears tHoldJump as soon as the sustain branch is no longer active.
    state.hold_jump_time_remaining = 0.0f;

    // After hold time expires (or jump released): apply gravity
    // OG model: single gravity value (600) with 0.5x multiplier when
    // jump held near apex. No separate rise/apex/fall bands.
    float gravity_mult = 1.0f;
    if ((input.jump_held || state.auto_jump) && std::fabs(state.velocity.y) < config.half_gravity_threshold) {
        gravity_mult = 0.5f;
    } else {
        state.auto_jump = false;
    }
    state.velocity.y = Approach(
        state.velocity.y,
        config.max_fall_speed,
        config.gravity * gravity_mult * delta_seconds
    );
}

}  // namespace

PlayerController::PlayerController(MovementConfig config)
    : config_(config), mover_(MakePlayerMoveConfig(config)) {
    // og-mover-overhaul Inc 2: the parity warning against PlayerMotorConfig is
    // gone (PlayerMotor is deleted). The single mover is MoveService (Inc 1);
    // MoveServiceConfig carries the same center-to-feet adapter as the controller.
}

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

    if (ConsumeWorldHitstopTick(state, delta_seconds)) return;

    BeginMoveTick(state);
    if (room != nullptr) {
        MovingSurfacePhase(state, *room);
    }

    // Full OG pipeline (Inc 2): timers/input -> state (velocity edits) ->
    // MovePhase (SweepMove + LateContact). MovePhase(nullptr) already routes
    // to the contact-snapshot-only path, so the room-less fixture case and
    // the full pipeline share one body (review C7: no dual branch).
    StepContext context = TimerInputPhase(state, input, camera_forward, delta_seconds);
    StatePhase(state, input, context, delta_seconds, room);
    MovePhase(state, delta_seconds, room, &context);
    CommitWorldHitstopRequest(state);
}

bool PlayerController::ConsumeWorldHitstopTick(PlayerState& state,
                                               float delta_seconds) const {
    if (delta_seconds <= 0.0f || state.world_hitstop_remaining <= 0.0f)
        return false;
    TickTimer(state.world_hitstop_remaining, delta_seconds);
    return true;
}

void PlayerController::RequestWorldHitstop(PlayerState& state,
                                           float duration) const {
    if (duration > 0.0f) state.world_hitstop_request = duration;
}

void PlayerController::CommitWorldHitstopRequest(PlayerState& state) const {
    if (state.world_hitstop_request > 0.0f)
        state.world_hitstop_remaining = state.world_hitstop_request;
    state.world_hitstop_request = 0.0f;
}

void PlayerController::MovingSurfacePhase(PlayerState& state, const Room& room) const {
    const int owner = state.movement_state == PlayerMovementState::Climbing
        ? state.climb.wall_surface_id : state.contact.ground_surface_id;
    if (owner < 0 || (state.movement_state != PlayerMovementState::Climbing && !state.contact.was_grounded)) return;
    const MovingSurface* surface = FindMovingSurface(room, owner);
    if (surface == nullptr) {
        state.contact.ground_surface_id = -1;
        return;
    }

    const Vec3& platform_velocity = surface->rider_velocity;
    RidingPlatformSetVelocity(state, platform_velocity,
                              surface->carry_storage_time);
    RidingPlatformMoved(state, room, surface->displacement);
}

void PlayerController::RidingPlatformSetVelocity(
    PlayerState& state, const Vec3& platform_velocity,
    float carry_storage_time) const {
    if (platform_velocity.x == 0.0f && platform_velocity.y == 0.0f &&
        platform_velocity.z == 0.0f) {
        return;
    }

    // OG RidingPlatformSetVelocity only replaces a live stored boost when the
    // new platform velocity is useful relative to player momentum.
    const float platform_speed_sq = LengthXZ(platform_velocity) * LengthXZ(platform_velocity);
    const float player_speed_sq = LengthXZ(state.velocity) * LengthXZ(state.velocity);
    const bool platform_turns_player =
        LengthXZ(platform_velocity) > 0.0f &&
        LengthXZ(state.velocity) > 0.0f &&
        DotXZ(platform_velocity, state.velocity) /
            (LengthXZ(platform_velocity) * LengthXZ(state.velocity)) < 0.5f;
    if (state.platform_carry.time_remaining < 0.0f ||
        platform_velocity.y >= state.velocity.y ||
        platform_speed_sq + 0.1f >= player_speed_sq ||
        platform_turns_player) {
        state.platform_carry.stored_velocity = platform_velocity;
        state.platform_carry.time_remaining = carry_storage_time > 0.0f
            ? carry_storage_time : 0.10f;
    }
}

void PlayerController::RidingPlatformMoved(PlayerState& state, const Room& room,
                                           const Vec3& displacement) const {
    // OG RidingPlatformMoved uses the same swept mover, with impact velocity
    // resolution disabled, so blocked riders do not teleport through walls.
    const Vec3 before = state.position;
    mover_.SweepMove(state, room, displacement, false);
    const Vec3 moved = {
        state.position.x - before.x,
        state.position.y - before.y,
        state.position.z - before.z,
    };
    state.climb.corner_from.x += moved.x;
    state.climb.corner_from.y += moved.y;
    state.climb.corner_from.z += moved.z;
    state.climb.corner_to.x += moved.x;
    state.climb.corner_to.y += moved.y;
    state.climb.corner_to.z += moved.z;
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
    context.camera_forward = camera_forward;
    context.camera_target_forward = NormalizeXZ(camera_forward, state.target_facing);
    context.has_move_input = context.raw_input_length > kEpsilon;
    state.contact.previous_velocity = state.velocity;
    // Don't update facing from move input while on a wall;
    // climbing uses vertical input but should keep the player facing into the wall.
    if (context.has_move_input && state.movement_state != PlayerMovementState::Climbing) {
        state.last_facing = context.move_input;
    }

    TickTimer(state.coyote_time_remaining, delta_seconds);
    TickTimer(state.hold_jump_time_remaining, delta_seconds);
    TickTimer(state.dash_refill_flash_remaining, delta_seconds);
    if (state.grounded) {
        context.dash_momentum = {};
        if (state.dash_reset_cooldown_remaining <= 0.0f) {
            RefillDash(state);
        }
    }

    // Jump/dash buffers belong to InputSystem::BeginTick. OG's global timers
    // decrement before state dispatch; state-local dash/skid timers stay in
    // their source handlers.

    TickTimer(state.dash_cooldown_remaining, delta_seconds);
    TickTimer(state.dash_reset_cooldown_remaining, delta_seconds);
    TickTimer(state.climb.cooldown_remaining, delta_seconds);
    TickTimer(state.no_move_time_remaining, delta_seconds);
    TickTimer(state.contact.ground_snap_cooldown_remaining, delta_seconds);
    TickTimer(state.platform_carry.time_remaining, delta_seconds);

    return context;
}

void PlayerController::StClimbingUpdate(
    PlayerState& state, const PlayerInput& input, StepContext& context,
    float delta_seconds, const Room* room
) const {
    const Vec3& move_input = context.move_input;
    const bool has_move_input = context.has_move_input;
    Vec3 climb_normal = state.climb.wall_normal;
    if (LengthXZ(climb_normal) < 0.1f)
        climb_normal = NormalizeXZ({-state.last_facing.x, 0, -state.last_facing.z});
    if (!input.climb_held) {
        climb_trace_event_ = 2;
        climb_trace_release_reason_ = 1;
        TransitionState(state, PlayerMovementState::Normal);
        state.locomotion_state = state.grounded ? LocomotionState::Idle : LocomotionState::Fall;
        return;
    }
        // Wall jump from climb: velocity is SET away from the wall (OG WallJump).
        if (input.ConsumeJumpPress()) {
            climb_trace_event_ = 2;
            climb_trace_release_reason_ = 4;
            TransitionState(state, PlayerMovementState::Normal);
            const Vec3 jump_dir = {-state.target_facing.x, 0.0f, -state.target_facing.z};
            // OG WallJump always uses the same SET launch. Climb input,
            // including holding a direction, does not select another profile.
            StartWallJump(state, config_, jump_dir);
            // No climb cooldown (OG parity): the facing-away gate blocks
            // instant re-grab after the launch.
            return;
        }

        // Dash off the wall (parity with OG dash-out of Climb). The device
        // owns the 0.10 s press buffer, so `input.dash_pressed` is the
        // buffered edge; the press edge is consumed on acceptance.
        if (state.dashes > 0 &&
            state.dash_cooldown_remaining <= 0.0f &&
            input.ConsumeDashPress()) {
            climb_trace_event_ = 2;
            climb_trace_release_reason_ = 5;
            if (has_move_input) {
                state.target_facing = move_input;
            }
            state.last_facing = state.target_facing;
            --state.dashes;
            TransitionState(state, PlayerMovementState::Dashing);
            state.locomotion_state = LocomotionState::Dash;
            context.dash_momentum = {state.velocity.x, 0.0f, state.velocity.z};
            return;
        }

    state.contact.ground_snap_cooldown_remaining = config_.ground_snap_cooldown;
        if (state.climb.corner_ease > 0.0f) {
            const float t = 1.0f - state.climb.corner_ease / config_.climb_corner_ease_time;
            const float u = Clamp(t, 0.0f, 1.0f);
            state.velocity = {};
            state.position = {
                state.climb.corner_from.x +
                    (state.climb.corner_to.x - state.climb.corner_from.x) * u,
                state.climb.corner_from.y +
                    (state.climb.corner_to.y - state.climb.corner_from.y) * u,
                state.climb.corner_from.z +
                    (state.climb.corner_to.z - state.climb.corner_from.z) * u,
            };
            // Shortest-arc facing lerp (OG Calc.AngleLerp parity).
            const float a_from = AngleXZ(state.climb.corner_facing_from);
            const float a_to = AngleXZ(state.climb.corner_facing_to);
            const float a_target = a_from + ShortestAngle(a_to - a_from) * u;
            state.target_facing = DirectionFromAngle(a_target);
            state.last_facing = state.target_facing;
            if (state.climb.corner_camera_active) {
                const float ca = AngleXZ(state.climb.corner_camera_from);
                const float cb = AngleXZ(state.climb.corner_camera_to);
                state.climb.camera_forward = DirectionFromAngle(ca + ShortestAngle(cb-ca) * u * 0.5f);
                state.climb.camera_forward_changed = true;
            }
            state.climb.corner_ease = Approach(
                state.climb.corner_ease, 0.0f, delta_seconds);
            state.locomotion_state = LocomotionState::Climb;
            return;
        }

    // OG latches the camera-dependent side sign until the stick recenters.
    if (std::fabs(input.move.x) < 0.5f)
        state.climb.input_sign = DotXZ(state.target_facing,
            NormalizeXZ(context.camera_target_forward)) < -0.4f ? -1 : 1;
    const float lateral = -input.move.x * state.climb.input_sign;
    const Vec3 tangent = ClimbWallRight(climb_normal);
    const Vec3 wall_up = ClimbWallUp(climb_normal);
    const Vec3 side_move = physics::Scale(tangent, lateral);
    const Vec3 forward = {state.target_facing.x, 0.0f, state.target_facing.z};
    Vec3 move = physics::Add(side_move, physics::Scale(wall_up, input.move.y));
    if (move.y < 0.0f && (room ? mover_.GroundCheck(state, *room, nullptr, nullptr)
                             : state.grounded))
        move.y = 0.0f;
    // OG Player.cs:1500-1503: climbing upward into a SpikeBlock cancels only
    // world-up motion. Map actor volumes preserve WorldBounds independent of
    // the lethal approach-direction gate used during ordinary player update.
    if (move.y > 0.0f && context.hazard_map != nullptr) {
        const Vec3 up_spike_probe = physics::Add(
            physics::Add(state.position, {0.0f, config_.climb_check_dist, 0.0f}),
            physics::Scale(forward, config_.climb_check_dist + 1.0f));
        if (AnySpikeBlockOverlapsAtPoint(*context.hazard_map, up_spike_probe))
            move.y = 0.0f;
    }
    // OG Player.cs:1505-1508: a lateral spike overlap removes side motion but
    // retains any simultaneous vertical climb component.
    if (context.hazard_map != nullptr) {
        const Vec3 side_spike_probe = physics::Add(
            physics::Add(ClimbWaistPoint(state, config_), side_move),
            physics::Scale(forward, config_.climb_check_dist + 1.0f));
        if (AnySpikeBlockOverlapsAtPoint(*context.hazard_map, side_spike_probe))
            move = physics::Sub(move, side_move);
    }
    if (std::fabs(move.x) < 0.1f) move.x = 0.0f;
    if (std::fabs(move.y) < 0.1f) move.y = 0.0f;
    if (std::fabs(move.z) < 0.1f) move.z = 0.0f;
    state.animation.Play(std::fabs(lateral) < 0.25f && input.move.y <= 0.0f
        ? PlayerAnimState::ClimbIdle : PlayerAnimState::ClimbUp);
    state.velocity = {move.x*config_.climb_speed, move.y*config_.climb_speed,
                      move.z*config_.climb_speed};
    context.movement.has_climb_delta = true;
    context.movement.climb_delta = {state.velocity.x * delta_seconds,
                                    state.velocity.y * delta_seconds,
                                    state.velocity.z * delta_seconds};
    // Climb is kinematic. A later mantle/walljump may write launch velocity,
    // which belongs exclusively to the ordinary outer sweep.
    state.velocity = {};
    context.climb_post_move = true;
    context.climb_tick_input = input.move;
    state.locomotion_state = LocomotionState::Climb;
}

bool PlayerController::TryClimb(PlayerState& state, const Vec3& relative_input,
                                const Room* room, StepContext* deferred) const {
    WallHit wall = ClimbCheckAt(state, config_, room, 0.0f, relative_input);
    if (!wall.hit && !state.grounded && state.velocity.y > 0.0f &&
        state.movement_state != PlayerMovementState::Climbing)
        wall = ClimbCheckAt(state, config_, room, 4.0f, relative_input);
    climb_trace_pending_.query_hit = wall.hit ? 1u : 0u;
    climb_trace_pending_.query_face = static_cast<int16_t>(wall.face_id);
    climb_trace_pending_.query_normal = wall.plane_normal;
    climb_trace_query_sampled_ = true;
    if (!wall.hit) {
        state.climb.wall_normal = {};
        state.climb.grip_face_id = -1;
        state.climb.wall_surface_id = -1;
        return false;
    }
    state.climb.wall_normal = wall.plane_normal;
    state.climb.grip_face_id = wall.face_id;
    state.climb.wall_surface_id = wall.owner_id;
    const Vec3 destination = {
        wall.point.x + wall.plane_normal.x*config_.climb_pushout_dist,
        wall.point.y + wall.plane_normal.y*config_.climb_pushout_dist +
            config_.player_half_height - config_.climb_waist_offset,
        wall.point.z + wall.plane_normal.z*config_.climb_pushout_dist};
    const Vec3 delta = physics::Sub(destination, state.position);
    if (deferred) {
        deferred->movement.has_climb_delta = true;
        deferred->movement.climb_delta = delta;
    } else {
        mover_.SweepMove(state, *room, delta, false);
    }
    state.target_facing = NormalizeXZ({-wall.plane_normal.x, 0, -wall.plane_normal.z});
    state.last_facing = state.target_facing;
    return true;
}

void PlayerController::StClimbingPostMove(
    PlayerState& state, StepContext& context, float delta_seconds, const Room* room
) const {
    (void)delta_seconds;
    const Vec3 from = state.position;
    const Vec3 facing_from = state.target_facing;
    const Vec3 normal_from = state.climb.wall_normal;
    const float lateral = -context.climb_tick_input.x * state.climb.input_sign;
    const Vec3 side = ClimbWallRight(normal_from);
    const Vec3 side_input = physics::Scale(side, lateral);
    bool force_corner = false;
    state.climb.corner_camera_active = false;
    WallHit slope{};
    WallHit adopted{};
    // OG source order is one mutually exclusive branch after the kinematic
    // sweep. A failed pre-corner grip query cannot release a valid handoff.
    if (room && lateral != 0.0f &&
        InnerCornerSideRay(state, config_, room, normal_from, side_input, &adopted)) {
        // Inner corner adopted by the bounded, player-side query.
    } else if (room && QueryWallRay(*room, ClimbWaistPoint(state, config_),
            {-normal_from.x, -normal_from.y, -normal_from.z},
            config_.climb_check_dist + 2.0f, &slope) &&
            std::fabs(slope.plane_normal.y) < 0.35f) {
        adopted = slope;
        AdoptCornerWall(state, config_, slope);
    } else if (room && lateral != 0.0f &&
        OuterCornerRay(state, config_, room, normal_from, side_input, {}, &adopted)) {
        state.animation.Play(PlayerAnimState::ClimbIdle);
        state.climb.corner_camera_from = context.camera_target_forward;
        state.climb.corner_camera_to = state.target_facing;
        state.climb.corner_camera_active = true;
        force_corner = true;
    } else if (room && context.climb_tick_input.y > 0.0f &&
        !ClimbCheckAt(state, config_, room, config_.climb_ledge_probe_extra,
                      context.move_input).hit) {
        TransitionState(state, PlayerMovementState::Normal);
        state.locomotion_state = LocomotionState::Jump;
        const Vec3 forward = NormalizeXZ(state.target_facing);
        state.velocity = {forward.x*config_.climb_hop_forward_speed,
                          config_.climb_hop_up_speed,
                          forward.z*config_.climb_hop_forward_speed};
        state.no_move_time_remaining = config_.climb_hop_no_move_time;
        state.climb.cooldown_remaining = config_.climb_cooldown;
        state.auto_jump = false;
        AddStoredPlatformVelocity(state);
        mover_.CancelGroundSnap(state);
        context.mantle_triggered = true;
        climb_trace_event_ = 5;
        return;
    } else if (room ? !TryClimb(state, context.move_input, room)
                    : !state.wall_contact) {
        TransitionState(state, PlayerMovementState::Normal);
        state.locomotion_state = state.grounded ? LocomotionState::Idle
                                               : LocomotionState::Fall;
        climb_trace_event_ = 2;
        climb_trace_release_reason_ = 3;
        return;
    }
    if (adopted.hit) {
        climb_trace_pending_.query_hit = 1u;
        climb_trace_pending_.query_face = static_cast<int16_t>(adopted.face_id);
        climb_trace_pending_.query_normal = adopted.plane_normal;
        climb_trace_query_sampled_ = true;
    }
    const Vec3 reanchor = physics::Sub(state.position, from);
    if (force_corner || physics::Dot(reanchor, reanchor) > 4.0f) {
        state.climb.corner_from = from;
        state.climb.corner_to = state.position;
        state.climb.corner_normal_from = normal_from;
        state.climb.corner_normal_to = state.climb.wall_normal;
        state.climb.corner_facing_from = facing_from;
        state.climb.corner_facing_to = state.target_facing;
        state.climb.corner_ease = config_.climb_corner_ease_time;
        state.position = from;
        state.target_facing = facing_from;
        state.last_facing = facing_from;
        context.corner_triggered = true;
        climb_trace_event_ = 6;
    }
    state.locomotion_state = LocomotionState::Climb;
}

bool PlayerController::TryDashAccept(
    PlayerState& state,
    const PlayerInput& input,
    StepContext& context
) const {
    if (state.dashes <= 0 || state.dash_cooldown_remaining > 0.0f ||
        !input.ConsumeDashPress()) {
        return false;
    }
    if (context.has_move_input) {
        state.target_facing = context.move_input;
    }
    state.last_facing = state.target_facing;
    --state.dashes;
    TransitionState(state, PlayerMovementState::Dashing);
    state.locomotion_state = LocomotionState::Dash;
    context.dash_momentum = {state.velocity.x, 0.0f, state.velocity.z};
    context.dash_started_this_tick = true;
    return true;
}

bool PlayerController::RefillDash(PlayerState& state, int amount) const {
    if (state.dashes >= amount) return false;
    state.dashes = amount;
    state.dash_refill_flash_remaining = 0.05f;
    return true;
}

// OG Spring parity (Player.cs:919-937). +Y-up mapping: OG's Z (up) is our Y;
// XY is the XZ travel plane. The vector approaches are OG Calc.Approach
// directional semantics (move along the direction by at most the amount),
// which is exactly what ApproachXZ implements.
void PlayerController::Spring(PlayerState& state, const Vec3& spring_position) const {
    // StateMachine.State invokes the current state's exit hook before Spring
    // writes its launch fields. This preserves StDashingExit's cooldown.
    TransitionState(state, PlayerMovementState::Normal);

    // The OG position is feet-anchored; this engine stores the body center.
    // Approach feet to board+3, then restore the center-to-feet offset.
    const float feet_y = state.position.y - config_.player_half_height;
    state.position.y = Approach(feet_y, spring_position.y + 3.0f, 2.0f) +
                       config_.player_half_height;
    state.position = ApproachXZ(
        state.position,
        {spring_position.x, state.position.y, spring_position.z},
        4.0f);

    // Launch: velocity pinned at spring_jump_speed for the hold window.
    state.hold_jump_speed = config_.spring_jump_speed;
    state.velocity.y = config_.spring_jump_speed;
    state.hold_jump_time_remaining = config_.spring_jump_hold_time;
    state.coyote_time_remaining = 0.0f;
    state.auto_jump = true;
    state.locomotion_state = LocomotionState::Jump;

    // Kill horizontal momentum (OG: Approach(velXY, zero, 30)).
    state.velocity = ApproachXZ(
        state.velocity,
        {0.0f, state.velocity.y, 0.0f},
        30.0f);

    // dashes = Max(dashes, 1): the launch refills a spent air dash.
    state.dashes = std::max(state.dashes, 1);
    // OG CancelGroundSnap parity: the launch must not be snap-pulled back.
    state.contact.ground_snap_cooldown_remaining = config_.ground_snap_cooldown;
}

void PlayerController::StNormalUpdate(
    PlayerState& state,
    const PlayerInput& input,
    StepContext& context,
    float delta_seconds,
    const Room* room
) const {
    const Vec3& move_input = context.move_input;
    const bool has_move_input = context.has_move_input;
    const float raw_input_length = context.raw_input_length;

    // OG StNormalUpdate performs horizontal movement first. A reverse input
    // that enters Skidding returns here, leaving a simultaneous dash press
    // buffered for StSkiddingUpdate on the next tick.
    if (state.movement_state == PlayerMovementState::Normal) {
        // Post-mantle input lockout (OG tNoMove, Player.cs:997): while the
        // lockout is active only friction applies — move input is ignored.
        const bool move_locked = state.no_move_time_remaining > 0.0f;
        const bool move_active = has_move_input && !move_locked;
        const Vec3 ground_move_input = state.grounded && move_active
            ? AvoidGroundLedge(state, config_, room, move_input)
            : move_input;
        const Vec3 horizontal_velocity = {state.velocity.x, 0.0f, state.velocity.z};
        const float speed_xz = LengthXZ(horizontal_velocity);
        // Base max speed, then apply slope modulation when grounded on a
        // non-flat normal (OG Player.cs:1017-1029): slopeDot grows with the
        // angle + along-slope input direction, scaling max UP or DOWN.
        float max_speed = config_.run_speed;
        if (state.grounded && (state.contact.ground_normal.x != 0.0f ||
            state.contact.ground_normal.y != 1.0f || state.contact.ground_normal.z != 0.0f)) {
            const float slope_dot_c =
                1.0f - Clamp(state.contact.ground_normal.y, 0.0f, 1.0f);
            // OG uses the current target-facing basis, before this tick's
            // steering update, to select uphill/downhill max-speed bias.
            const Vec3 gxz = NormalizeXZ(
                {state.contact.ground_normal.x, 0.0f, state.contact.ground_normal.z},
                state.target_facing);
            const float along = DotXZ(gxz, state.target_facing);
            max_speed += max_speed * (slope_dot_c * along * 2.0f);
        }
        const float true_max_speed = max_speed;
        const float desired_speed = max_speed * AnalogMagnitude(raw_input_length);
        const Vec3 ground_target_velocity = move_active
            ? Vec3{ground_move_input.x * desired_speed, state.velocity.y, ground_move_input.z * desired_speed}
            : Vec3{0.0f, state.velocity.y, 0.0f};

        if (state.grounded) {
            // OG: rotation-based turning when above rotate threshold.
            if (move_active && speed_xz >= config_.rotate_threshold) {
                // Skid trigger (og-mover-overhaul Inc 4, OG Player.cs:1072-1078):
                // when moving fast AND the camera-relative input opposes the
                // velocity direction past SkidDotThreshold, enter Skidding.
                const Vec3 vel_norm = NormalizeXZ(horizontal_velocity, ground_move_input);
                const float turn_dot = DotXZ(vel_norm, ground_move_input);
                if (turn_dot <= config_.skid_dot_threshold) {
                    TransitionState(state, PlayerMovementState::Skidding);
                    state.no_skid_jump_remaining = config_.no_skid_jump_time;
                    state.target_facing = ground_move_input;
                    state.facing = ground_move_input;
                    state.locomotion_state = LocomotionState::Idle;
                    return;
                }
                const bool past_max = speed_xz >= true_max_speed &&
                    DotXZ(ground_move_input, horizontal_velocity) >= 0.7f;
                const float accel = past_max ? config_.past_max_deceleration
                                             : config_.acceleration;
                const float rotate_speed = speed_xz > true_max_speed
                    ? config_.rotate_speed_above_max
                    : config_.rotate_speed;
                state.target_facing = RotateTowardXZ(
                    state.target_facing, ground_move_input,
                    rotate_speed * delta_seconds);
                const float next_speed = Approach(
                    speed_xz, desired_speed, accel * delta_seconds);
                state.velocity.x = state.target_facing.x * next_speed;
                state.velocity.z = state.target_facing.z * next_speed;
            } else {
                const bool past_max = move_active &&
                    speed_xz >= true_max_speed &&
                    DotXZ(ground_move_input, horizontal_velocity) >= 0.7f;
                const float accel = !move_active ? config_.friction
                    : (past_max ? config_.past_max_deceleration
                                : config_.acceleration);
                state.velocity = ApproachXZ(state.velocity, ground_target_velocity, accel * delta_seconds);
                if (move_active) {
                    state.target_facing = ground_move_input;
                }
            }
            state.locomotion_state = move_active || speed_xz > kEpsilon
                ? LocomotionState::Run
                : LocomotionState::Idle;
        } else {
            if (move_active) {
                // OG normalizes RelativeMoveInput in air and targets full
                // MaxSpeed; analog magnitude remap applies only while grounded.
                const Vec3 air_target_velocity = {
                    move_input.x * config_.run_speed,
                    state.velocity.y,
                    move_input.z * config_.run_speed,
                };

                // OG air steering scales against targetFacing, not current
                // velocity direction. PastMaxDeccel is used only when speed
                // is at/above MaxSpeed AND input continues along velocity.
                const float facing_dot = std::clamp(
                    DotXZ(move_input, state.target_facing), -1.0f, 1.0f);
                const float facing_t = (facing_dot + 1.0f) * 0.5f;
                const bool past_max = speed_xz >= config_.run_speed &&
                    DotXZ(move_input, NormalizeXZ(horizontal_velocity,
                                                  state.target_facing)) >= 0.7f;
                const float accel_mult = past_max
                    ? config_.air_accel_mult_max -
                          (config_.air_accel_mult_max - config_.air_accel_mult_min) * facing_t
                    : config_.air_accel_mult_min +
                          (config_.air_accel_mult_max - config_.air_accel_mult_min) * facing_t;
                const float accel = past_max
                    ? config_.past_max_deceleration * accel_mult
                    : config_.acceleration * accel_mult;
                state.velocity = ApproachXZ(
                    state.velocity, air_target_velocity, accel * delta_seconds);
            } else {
                // OG's no-input/tNoMove path applies friction in air too,
                // scaled by AirFrictionMult (0.1), instead of preserving
                // horizontal velocity indefinitely.
                const float air_friction = config_.friction * config_.air_friction_mult;
                state.velocity = ApproachXZ(
                    state.velocity,
                    {0.0f, state.velocity.y, 0.0f},
                    air_friction * delta_seconds);
            }
            state.locomotion_state = state.velocity.y > 0.0f ? LocomotionState::Jump : LocomotionState::Fall;
        }

    }

    // OG order after ordinary horizontal movement: climb, then dash, then
    // jump/gravity. The input edge remains buffered if an earlier state path
    // (such as the skid transition above) returns before reaching this point.
    if (state.movement_state == PlayerMovementState::Normal && input.climb_held &&
        state.climb.cooldown_remaining <= 0.0f) {
        const bool grab = room ? TryClimb(state, context.move_input, room, &context)
            : (state.wall_contact && (!context.has_move_input ||
                DotXZ(context.move_input, NormalizeXZ(state.wall_normal,
                    {-state.last_facing.x, 0.0f, -state.last_facing.z})) <= -0.5f));
        if (grab) {
            if (!room) state.climb.wall_normal = NormalizeXZ(state.wall_normal,
                {-state.last_facing.x, 0.0f, -state.last_facing.z});
            climb_trace_event_ = 1;
            TransitionState(state, PlayerMovementState::Climbing);
            state.locomotion_state = LocomotionState::Climb;
            state.velocity = {};
            return;
        }
    }

    // Shared by Normal and Skidding, matching the source's two TryDash sites.
    if (state.movement_state == PlayerMovementState::Normal &&
        TryDashAccept(state, input, context)) {
        return;  // launch now; Dashing updates starting next tick
    }

    if (state.movement_state == PlayerMovementState::Normal) {
        // OG order: ordinary horizontal movement first, then coyote jump,
        // then directional WallJumpCheck, and only then gravity. A wall-jump
        // launch therefore cannot be altered by this tick's air friction or
        // acceleration. Coyote jump keeps priority when both are possible.
        const bool coyote_jump_available = state.coyote_time_remaining > 0.0f &&
                                           input.JumpPressAvailable();
        bool did_wall_jump = false;
        if (!coyote_jump_available && input.JumpPressAvailable()) {
            bool wall_jump = false;
            Vec3 jump_dir{};
            float pushout = 0.0f;
            if (room != nullptr) {
                const Vec3 into_wall = {-state.target_facing.x, 0.0f, -state.target_facing.z};
                const WallHit hit = QueryWallClosestToFace(
                    *room, ClimbWaistPoint(state, config_),
                    config_.climb_check_dist, into_wall);
                if (hit.hit) {
                    wall_jump = true;
                    jump_dir = NormalizeXZ(hit.plane_normal,
                        Vec3{-state.last_facing.x, 0.0f, -state.last_facing.z});
                    // OG WallJumpCheck pushes out by
                    // WallPushoutDist / ClimbCheckDist before launching.
                    pushout = hit.pushout *
                              (config_.climb_pushout_dist / config_.climb_check_dist);
                    state.position.x += hit.normal.x * pushout;
                    state.position.y += hit.normal.y * pushout;
                    state.position.z += hit.normal.z * pushout;
                }
            } else if (state.wall_contact) {
                wall_jump = true;
                jump_dir = NormalizeXZ(state.wall_normal,
                    Vec3{-state.last_facing.x, 0.0f, -state.last_facing.z});
            }
            if (wall_jump && input.ConsumeJumpPress()) {
                StartWallJump(state, config_, jump_dir);
                did_wall_jump = true;
            }
        }
        if (!did_wall_jump) {
            ApplyJumpAndGravity(state, input, move_input, config_, delta_seconds);
        }
    }
    if (state.grounded) {
        const float speed = LengthXZ(state.velocity);
        state.animation.Play(speed * speed > 1.0f ? PlayerAnimState::Run : PlayerAnimState::Idle);
        state.animation.rate = speed * speed > 1.0f
            ? 0.1f + Clamp(speed / (config_.run_speed * 2.0f), 0.0f, 1.0f) * 2.9f : 1.0f;
    } else {
        state.animation.Clear();
        state.animation.Play(PlayerAnimState::Run);
    }

}

void PlayerController::StDashingUpdate(
    PlayerState& state,
    const PlayerInput& input,
    StepContext& context,
    float delta_seconds,
    const Room* room
) const {
    state.animation.Play(PlayerAnimState::Dash);
    const Vec3& move_input = context.move_input;
    const bool has_move_input = context.has_move_input;
    (void)room;
    if (state.movement_state == PlayerMovementState::Dashing) {
            // OG StDashingEnter applies launch velocity before World.HitStun
            // pauses later world ticks. A direct same-tick invocation on
            // the same demake tick, so leave the entry-frame launch intact.
            if (context.dash_started_this_tick) {
                return;
            }
            state.dash_time_remaining -= delta_seconds;
            if (state.dash_time_remaining <= 0.0f) {
                if (!state.grounded) {
                    state.velocity.x *= config_.dash_end_speed_multiplier;
                    state.velocity.y *= config_.dash_end_speed_multiplier;
                    state.velocity.z *= config_.dash_end_speed_multiplier;
                }
                state.dash_active_remaining = 0.0f;
                TransitionState(state, PlayerMovementState::Normal);
                return;
            }

            TickTimer(state.dash_active_remaining, delta_seconds);
            // Dash steering (og-mover-overhaul Inc 4, OG Player.cs:1288-1293):
            // while dashing, rotate toward move input and re-set dash speed so
            // the player can arc the dash. OG: steer while Dot(move, facing) >= -0.2.
            const Vec3 dash_dir = state.target_facing;
            if (has_move_input && (-input.move.x * dash_dir.x + input.move.y * dash_dir.z) >= -0.2f) {
                state.target_facing = RotateTowardXZ(
                    dash_dir, move_input, config_.dash_rotate_speed * delta_seconds);
                SetDashVelocity(state, config_);
            }
            // Preserve collision-reduced momentum when no steering input is
            // accepted; DashJump consumes the velocity left by this tick.
            context.dash_momentum.x = state.velocity.x;
            context.dash_momentum.z = state.velocity.z;
            TickTimer(state.no_dash_jump_remaining, delta_seconds);
            const bool dash_jump_window = state.dashed_on_ground &&
                state.coyote_time_remaining > 0.0f &&
                state.no_dash_jump_remaining <= 0.0f;
            if (dash_jump_window && input.ConsumeJumpPress()) {
                TransitionState(state, PlayerMovementState::Normal);
                StartDashJump(state, config_, move_input, context);
            }
        }
}

void PlayerController::StSkiddingUpdate(
    PlayerState& state,
    const PlayerInput& input,
    StepContext& context,
    float delta_seconds,
    const Room* room
) const {
    const Vec3& move_input = context.move_input;
    const bool has_move_input = context.has_move_input;
    (void)room;
    TickTimer(state.no_skid_jump_remaining, delta_seconds);
    // OG StSkiddingUpdate runs TryDash() FIRST (Player.cs:1373): a dash press
    // while skidding fires a dash and returns without a Dashing update.
    if (TryDashAccept(state, input, context)) {
        return;  // launch now; Dashing updates starting next tick
    }
    // Skidding state (og-mover-overhaul Inc 4, OG StSkiddingUpdate
    // Player.cs:1371-1429). Skid decel toward the input direction; exit on
    // (a) no/low input, (b) skid-done (aligned + up to EndSkidSpeed),
    // (c) leaving the ground, (d) skid-jump into Normal.
    if (state.movement_state == PlayerMovementState::Skidding) {
            const Vec3 skid_vel = {state.velocity.x, 0.0f, state.velocity.z};
            const Vec3 skid_norm = NormalizeXZ(skid_vel, Vec3{});
            // cancel conditions (OG: input < .2, dot < .7 with facing, or !onGround)
            const float mag2 = physics::Len2(move_input);
            const float input_to_facing = has_move_input ? DotXZ(move_input, state.target_facing) : 0.0f;
            if (mag2 < 0.2f * 0.2f || (has_move_input && input_to_facing < 0.7f) || !state.grounded) {
                TransitionState(state, PlayerMovementState::Normal);
            } else {
                // skid-jump
                if (state.no_skid_jump_remaining <= 0.0f && input.ConsumeJumpPress()) {
                    TransitionState(state, PlayerMovementState::Normal);
                    // OG SkidJump restores the recorded coyote height and
                    // consumes coyote time. Without this, a second fixed tick
                    // in the same render frame can treat the skid jump as a
                    // fresh coyote jump if an input snapshot is replayed.
                    state.position.y = state.contact.coyote_height;
                    state.coyote_time_remaining = 0.0f;
                    // OG CancelGroundSnap parity: the skid-jump launch must not
                    // be snap-pulled back to the floor.
                    state.contact.ground_snap_cooldown_remaining = config_.ground_snap_cooldown;
                    state.velocity.x = state.target_facing.x * config_.skid_jump_xy_speed;
                    state.velocity.z = state.target_facing.z * config_.skid_jump_xy_speed;
                    state.model_scale = {0.6f, 1.4f, 0.6f};
                    state.velocity.y = config_.skid_jump_speed;
                    state.hold_jump_speed = config_.skid_jump_speed;
                    state.hold_jump_time_remaining = config_.skid_jump_hold_time;
                    AddStoredPlatformVelocity(state);
                    state.locomotion_state = LocomotionState::Jump;
                } else {
                    // decel toward input direction
                    const bool dot_matches = DotXZ(skid_norm, state.target_facing) >= 0.7f;
                    const float accel = dot_matches ? config_.skidding_accel
                                                    : config_.skidding_start_accel;
                    const Vec3 target = {
                        move_input.x * config_.run_speed,
                        state.velocity.y,
                        move_input.z * config_.run_speed,
                    };
                    state.velocity = ApproachXZ(state.velocity, target, accel * delta_seconds);
                    // reached EndSkidSpeed while aligned → exit to Normal
                    if (dot_matches && LengthXZ({state.velocity.x, 0.0f, state.velocity.z}) >= config_.end_skid_speed) {
                        TransitionState(state, PlayerMovementState::Normal);
                    }
                }
            }
        }
}

void PlayerController::TransitionState(PlayerState& state, PlayerMovementState next) const {
    const PlayerMovementState previous = state.movement_state;
    if (previous == next) return;
    OnStateExit(state, previous, next);
    state.movement_state = next;
    OnStateEnter(state, previous, next);
}

void PlayerController::OnStateEnter(PlayerState& state, PlayerMovementState /*prev*/,
                                     PlayerMovementState next) const {
    switch (next) {
        case PlayerMovementState::Normal:
            state.hold_jump_time_remaining = 0.0f;
            break;
        case PlayerMovementState::Climbing:
            state.animation.Play(PlayerAnimState::ClimbIdle, true);
            state.animation.rate = 1.8f;
            state.velocity = {};
            state.climb.corner_ease = 0.0f;
            state.climb.corner_camera_active = false;
            state.climb.input_sign = 1;
            break;
        case PlayerMovementState::Skidding:
            state.no_skid_jump_remaining = config_.no_skid_jump_time;
            state.animation.Play(PlayerAnimState::Skid, true);
            break;
        case PlayerMovementState::Dashing:
            state.facing = state.target_facing;
            state.dashed_on_ground = state.grounded;
            SetDashVelocity(state, config_);
            state.auto_jump = true;
            RequestWorldHitstop(state, config_.dash_hitstop_time);
            state.dash_time_remaining = config_.dash_duration;
            state.dash_active_remaining = config_.dash_duration;
            state.dash_reset_cooldown_remaining = config_.dash_reset_cooldown;
            state.no_dash_jump_remaining = config_.no_dash_jump_time;
            // OG deliberately leaves CancelGroundSnap disabled for dashes.
            break;
    }
}

void PlayerController::OnStateExit(PlayerState& state, PlayerMovementState prev,
                                    PlayerMovementState /*next*/) const {
    switch (prev) {
        case PlayerMovementState::Normal:
            state.hold_jump_time_remaining = 0.0f;
            state.no_move_time_remaining = 0.0f;
            state.auto_jump = false;
            state.animation.rate = 1.0f;
            break;
        case PlayerMovementState::Dashing:
            state.dash_cooldown_remaining = config_.dash_cooldown;
            break;
        case PlayerMovementState::Climbing:
            state.animation.Play(PlayerAnimState::Idle);
            state.animation.rate = 1.0f;
            state.climb.wall_surface_id = -1;
            break;
        case PlayerMovementState::Skidding:
            state.animation.Play(PlayerAnimState::Idle, true);
            break;
    }
}

void PlayerController::StatePhase(
    PlayerState& state,
    const PlayerInput& input,
    StepContext& context,
    float delta_seconds,
    const Room* room
) const {
    if (delta_seconds <= 0.0f) {
        return;
    }

    ++climb_trace_tick_;
    climb_trace_pending_ = {};
    climb_trace_pending_valid_ = true;
    climb_trace_pending_.tick = climb_trace_tick_;
    climb_trace_pending_.input_climb = input.climb_held ? 1u : 0u;
    climb_trace_pending_.move_input = input.move;
    climb_trace_pending_.raw_z = input.raw_climb_z;
    climb_trace_pending_.raw_l = input.raw_climb_l;
    climb_trace_pending_.raw_r = input.raw_climb_r;
    climb_trace_pending_.state_before = static_cast<uint8_t>(state.movement_state);
    climb_trace_pending_.position_before = state.position;
    climb_trace_pending_.velocity_before = state.velocity;
    climb_trace_pending_.anchor = state.position;
    climb_trace_event_ = 0;
    climb_trace_release_reason_ = 0;
    climb_trace_query_sampled_ = false;

    // OG StateMachine.Update invokes only the state active at tick entry.
    // Enter/exit run synchronously after its transition; the new handler
    // cannot consume time or apply friction until the following tick.
    const PlayerMovementState before = state.movement_state;
    ++context.state_handler_count;
    switch (before) {
        case PlayerMovementState::Normal:
            StNormalUpdate(state, input, context, delta_seconds, room); break;
        case PlayerMovementState::Dashing:
            StDashingUpdate(state, input, context, delta_seconds, room); break;
        case PlayerMovementState::Skidding:
            StSkiddingUpdate(state, input, context, delta_seconds, room); break;
        case PlayerMovementState::Climbing:
            StClimbingUpdate(state, input, context, delta_seconds, room); break;
    }

}

void PlayerController::MovePhase(
    PlayerState& state,
    float delta_seconds,
    const Room* room,
    StepContext* context
) const {
    MoveUpdatePhase(state, delta_seconds, room, context);
    LateMoveContactPhase(state, delta_seconds, room);
}

void PlayerController::MoveUpdatePhase(
    PlayerState& state,
    float delta_seconds,
    const Room* room,
    StepContext* context
) const {
    if (delta_seconds <= 0.0f) {
        return;
    }
    Vec3 collision_correction{};
    auto accumulate_correction = [&](const Vec3& before, const Vec3& delta) {
        collision_correction.x += state.position.x - before.x - delta.x;
        collision_correction.y += state.position.y - before.y - delta.y;
        collision_correction.z += state.position.z - before.z - delta.z;
    };
    if (context && context->movement.has_climb_delta &&
        state.movement_state == PlayerMovementState::Climbing) {
        const Vec3 delta = context->movement.climb_delta;
        if (room) {
            const Vec3 before = state.position;
            mover_.SweepMove(state, *room, delta, false);
            accumulate_correction(before, delta);
        }
        if (context) context->movement_order = context->movement_order * 10 + 1;
    }
    // OG corner/sustain/lip queries observe the resolved kinematic position.
    if (context && context->climb_post_move &&
        state.movement_state == PlayerMovementState::Climbing) {
        StClimbingPostMove(state, *context, delta_seconds, room);
        context->climb_post_move = false;
    }
    const Vec3 amount = {state.velocity.x * delta_seconds,
                         state.velocity.y * delta_seconds,
                         state.velocity.z * delta_seconds};
    if (room) {
        const Vec3 before = state.position;
        mover_.SweepMove(state, *room, amount, state.no_move_time_remaining <= 0.0f);
        accumulate_correction(before, amount);
    }
    if (context) context->movement_order = context->movement_order * 10 + 2;
    if (room) {
        const Vec3 before = state.position;
        mover_.Popout(state, *room, false);
        accumulate_correction(before, {});
    }
    if (context) context->movement_order = context->movement_order * 10 + 3;
    // Source corner/anchor writes between sweeps are deliberately excluded:
    // this field measures shared resolver correction, not state displacement.
    climb_trace_pending_.collision_correction = collision_correction;
}

void PlayerController::LateMoveContactPhase(
    PlayerState& state,
    float delta_seconds,
    const Room* room
) const {
    if (delta_seconds <= 0.0f) return;
    if (room == nullptr) {
        LateContactPhase(state, delta_seconds, nullptr);
        FinishClimbTrace(state);
        return;
    }

    // OG LateUpdate ground checks (Player.cs:400-411): recompute grounded from
    // the post-move position, then ground snap if we left the ground.
    const bool prev_grounded = state.contact.was_grounded;
    GroundHit ground_hit{};
    Vec3 ground_pushout{}, ground_normal{};
    state.grounded = mover_.GroundCheck(state, *room, &ground_pushout, &ground_normal, &ground_hit);
    if (state.grounded) state.position = physics::Add(state.position, ground_pushout);
    if (!state.grounded && prev_grounded) {
        mover_.GroundSnap(state, *room, delta_seconds);
        if (state.grounded) {
            mover_.GroundCheck(state, *room, nullptr, &ground_normal, &ground_hit);
        }
    }
    state.contact.ground_normal = state.grounded ? ground_normal : Vec3{0.0f, 1.0f, 0.0f};
    if (!prev_grounded && state.grounded) {
        const float impact = Clamp(state.contact.previous_velocity.y / config_.max_fall_speed, 0.0f, 1.0f);
        state.model_scale = {1.0f + 0.4f * impact, 1.0f - 0.4f * impact, 1.0f + 0.4f * impact};
    }
    if (state.grounded && ground_hit.hit &&
        FindMovingSurface(*room, ground_hit.owner_id) != nullptr) {
        state.contact.ground_surface_id = ground_hit.owner_id;
    } else {
        state.contact.ground_surface_id = -1;
    }

    // Coyote refresh + dash refill when grounded (OG: tCoyote reset, dash
    // refill on ground within reset window).
    if (state.grounded) {
        // OG LateUpdate clears autoJump on every grounded contact, including
        // the frame where GroundSnap restores support after stepping down.
        state.auto_jump = false;
        state.coyote_time_remaining = config_.coyote_time;
        state.contact.coyote_height = state.position.y;
        if (state.dash_reset_cooldown_remaining <= 0.0f) {
            RefillDash(state);
        }
    }

    LateContactPhase(state, delta_seconds, room);
    FinishClimbTrace(state);
}

void PlayerController::FinishClimbTrace(const PlayerState& state) const {
    if (!climb_trace_pending_valid_) return;
    climb_trace_pending_.state_after = static_cast<uint8_t>(state.movement_state);
    climb_trace_pending_.position_after = state.position;
    climb_trace_pending_.velocity_after = state.velocity;
    climb_trace_pending_.event = climb_trace_event_;
    climb_trace_pending_.release_reason = climb_trace_release_reason_;

    const bool was_climbing = climb_trace_pending_.state_before ==
        static_cast<uint8_t>(PlayerMovementState::Climbing);
    const bool is_climbing = state.movement_state == PlayerMovementState::Climbing;
    if (climb_trace_pending_.event == 0 && !was_climbing && is_climbing)
        climb_trace_pending_.event = 1;
    if (climb_trace_pending_.event == 0 && was_climbing && !is_climbing)
        climb_trace_pending_.event = 2;

    if (was_climbing && climb_trace_pending_.input_climb &&
        climb_trace_query_sampled_ && !climb_trace_pending_.query_hit) {
        if (climb_trace_grip_miss_ticks_ < 255) ++climb_trace_grip_miss_ticks_;
        if (climb_trace_grip_miss_ticks_ == 2 && climb_trace_pending_.event == 0)
            climb_trace_pending_.event = 3;
    } else {
        climb_trace_grip_miss_ticks_ = 0;
    }

    const Vec3 total_delta = {
        state.position.x - climb_trace_pending_.position_before.x,
        state.position.y - climb_trace_pending_.position_before.y,
        state.position.z - climb_trace_pending_.position_before.z,
    };
    const float total_delta_sq = total_delta.x * total_delta.x +
                                 total_delta.y * total_delta.y +
                                 total_delta.z * total_delta.z;
    if (climb_trace_pending_.event == 0 && total_delta_sq > 25.0f)
        climb_trace_pending_.event = 4;

#ifdef N64
    RecordClimbTraceFrame(climb_trace_pending_);
#endif
    climb_trace_pending_valid_ = false;
}

void PlayerController::LateContactPhase(
    PlayerState& state,
    float delta_seconds,
    const Room* room
) const {
    (void)delta_seconds;
    (void)room;
    state.contact.was_grounded = state.grounded;
    state.model_scale.x = Approach(state.model_scale.x, 1.0f, delta_seconds / 0.8f);
    state.model_scale.y = Approach(state.model_scale.y, 1.0f, delta_seconds / 0.8f);
    state.model_scale.z = Approach(state.model_scale.z, 1.0f, delta_seconds / 0.8f);
    state.facing = RotateTowardXZ(state.facing, state.target_facing, 12.566370614f * delta_seconds);
    state.animation.Advance(delta_seconds);
}

}  // namespace madeline_cube
