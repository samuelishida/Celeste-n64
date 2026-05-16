#include "player_motor.hpp"

#include <cmath>

namespace madeline_cube {
namespace {

constexpr float kGroundSkin = 0.001f;

float Length(const Vec3& v) {
    return std::sqrt((v.x * v.x) + (v.y * v.y) + (v.z * v.z));
}

void Sanitize(Vec3& v) {
    if (!IsNumericValid(v.x)) v.x = 0.0f;
    if (!IsNumericValid(v.y)) v.y = 0.0f;
    if (!IsNumericValid(v.z)) v.z = 0.0f;
}

void RemoveIntoNormal(Vec3& vel, const Vec3& n) {
    const float dot = (vel.x * n.x) + (vel.y * n.y) + (vel.z * n.z);
    if (dot >= 0.0f) return;
    vel.x -= n.x * dot;
    vel.y -= n.y * dot;
    vel.z -= n.z * dot;
}

void RecordWallContact(PlayerState& state, MotorResult& result, const WallHit& wall) {
    result.wall_contact = true;
    result.wall_face_id = wall.face_id;
    result.wall_normal = wall.normal;
    if (wall.normal.x > 0.5f) state.wall_left = true;
    if (wall.normal.x < -0.5f) state.wall_right = true;
}

GroundHit ProbeFloor(const Room& room, const Vec3& position, float half_height, float probe_distance) {
    const Vec3 feet = {position.x, position.y - half_height, position.z};
    return QueryFloorSource(room, feet, probe_distance);
}

void ApplyGroundContact(PlayerState& state, MotorResult& result, const GroundHit& hit, float half_height) {
    state.position.y = hit.point.y + half_height;
    if (state.velocity.y < 0.0f) state.velocity.y = 0.0f;
    state.grounded = true;
    state.contact.ground_normal = hit.normal;
    state.contact.coyote_height = state.position.y;
    result.ground_face_id = hit.face_id;
    result.ground_normal = hit.normal;
}

}  // namespace

PlayerMotor::PlayerMotor(PlayerMotorConfig config) : config_(config) {}

MotorResult PlayerMotor::Step(PlayerState& state, const Room& room, const MotorInput& input, float delta_seconds) const {
    Sanitize(state.position);
    state.velocity = input.requested_velocity;
    Sanitize(state.velocity);

    const bool was_grounded = state.grounded;
    state.contact.was_grounded = was_grounded;
    state.wall_left = false;
    state.wall_right = false;

    MotorResult result;
    result.position = state.position;
    result.velocity = state.velocity;

    const Vec3 delta = {
        state.velocity.x * delta_seconds,
        state.velocity.y * delta_seconds,
        state.velocity.z * delta_seconds,
    };
    const float distance = Length(delta);
    const int steps = distance > 0.0f
        ? static_cast<int>(std::ceil(distance / config_.sweep_step))
        : 0;

    Vec3 step_vec = {0.0f, 0.0f, 0.0f};
    if (steps > 0) {
        step_vec = {delta.x / steps, delta.y / steps, delta.z / steps};
    }

    // Tracks whether a substep already established ground contact.  When true
    // the late-pass probe extends its tolerance to one sweep_step so that
    // horizontal substeps after grounding still re-pin to the slope below
    // (slope-follow without slope-specific code in the substep loop).
    bool grounded_mid_sweep = false;
    for (int i = 0; i < steps; ++i) {
        const float prev_feet_y = state.position.y - config_.half_height;
        const float prev_head_y = state.position.y + config_.half_height;
        state.position.x += step_vec.x;
        state.position.y += step_vec.y;
        state.position.z += step_vec.z;

        if (step_vec.y < 0.0f) {
            const Vec3 feet_origin = {state.position.x, prev_feet_y, state.position.z};
            const float probe = -step_vec.y + kGroundSkin;
            const GroundHit floor = QueryFloorSource(room, feet_origin, probe);
            if (floor.hit) {
                ApplyGroundContact(state, result, floor, config_.half_height);
                step_vec.y = 0.0f;
                grounded_mid_sweep = true;
            }
        } else if (step_vec.y > 0.0f) {
            const Vec3 head_origin = {state.position.x, prev_head_y, state.position.z};
            const float probe = step_vec.y + kGroundSkin;
            const CeilingHit ceiling = QueryCeilingSource(room, head_origin, probe);
            if (ceiling.hit) {
                state.position.y = ceiling.point.y - config_.half_height;
                state.velocity.y = 0.0f;
                step_vec.y = 0.0f;
            }
        }

        const WallHit wall = QueryWallNearest(room, state.position, config_.radius);
        if (wall.hit && wall.pushout > 0.0f) {
            state.position.x += wall.normal.x * wall.pushout;
            state.position.y += wall.normal.y * wall.pushout;
            state.position.z += wall.normal.z * wall.pushout;
            RemoveIntoNormal(state.velocity, wall.normal);
            RecordWallContact(state, result, wall);
        }
    }

    state.grounded = false;
    result.ground_face_id = -1;
    result.ground_normal = {0.0f, 1.0f, 0.0f};

    const GroundHit ground = ProbeFloor(room, state.position, config_.half_height,
                                        config_.ground_contact_tolerance);
    if (ground.hit) {
        ApplyGroundContact(state, result, ground, config_.half_height);
    } else if (grounded_mid_sweep) {
        // Slope-follow: a previous substep grounded, but horizontal substeps
        // walked the player slightly above the slope (the substep ground
        // capture only zeros vertical velocity).  Re-probe with one sweep_step
        // of tolerance so the post-grounding horizontal travel stays pinned.
        const GroundHit follow = ProbeFloor(room, state.position, config_.half_height,
                                            config_.sweep_step + kGroundSkin);
        if (follow.hit) {
            ApplyGroundContact(state, result, follow, config_.half_height);
        }
    } else if (input.wants_ground_snap &&
               was_grounded &&
               state.contact.ground_snap_cooldown_remaining <= 0.0f) {
        const GroundHit snap = ProbeFloor(room, state.position, config_.half_height,
                                          config_.ground_snap_distance);
        if (snap.hit) {
            ApplyGroundContact(state, result, snap, config_.half_height);
        }
    }

    if (state.velocity.y > 0.0f) {
        const Vec3 head = {state.position.x, state.position.y + config_.half_height, state.position.z};
        const CeilingHit ceiling = QueryCeilingSource(room, head, config_.ground_contact_tolerance);
        if (ceiling.hit) {
            state.position.y = ceiling.point.y - config_.half_height;
            state.velocity.y = 0.0f;
        }
    }

    if (!result.wall_contact) {
        const WallHit settled = QueryWallNearest(room, state.position, config_.radius + 0.05f);
        if (settled.hit) {
            RecordWallContact(state, result, settled);
        }
    }

    if (state.grounded && ground.hit && ground.owner_id >= 0) {
        const Vec3 owner_v = ground.owner_velocity;
        const float speed_sq = (owner_v.x * owner_v.x) + (owner_v.y * owner_v.y) + (owner_v.z * owner_v.z);
        if (speed_sq > 0.0f) {
            state.platform_carry.stored_velocity = owner_v;
            state.platform_carry.time_remaining = config_.platform_carry_storage_time;
        }
    }

    Sanitize(state.position);
    Sanitize(state.velocity);

    result.position = state.position;
    result.velocity = state.velocity;
    result.grounded = state.grounded;
    result.landed_this_frame = state.grounded && !was_grounded;
    return result;
}

MotorResult PlayerMotor::RefreshContacts(PlayerState& state, const Room& room, const MotorInput& input) const {
    const MotorInput zero_input{
        .requested_velocity = {0.0f, 0.0f, 0.0f},
        .wants_ground_snap = input.wants_ground_snap,
        .wants_coyote_refresh = input.wants_coyote_refresh,
        .wants_dash_refill = input.wants_dash_refill,
    };
    state.velocity = {0.0f, 0.0f, 0.0f};
    return Step(state, room, zero_input, 0.0f);
}

}  // namespace madeline_cube
