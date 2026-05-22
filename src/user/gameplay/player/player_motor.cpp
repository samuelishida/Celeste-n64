#include "gameplay/player/player_motor.hpp"

#include <cmath>

#include "gameplay/physics/coll_mesh.hpp"
#include "gameplay/world/world.hpp"

#ifdef __mips__
#include <libdragon.h>
#define PM_LOG debugf
#else
#define PM_LOG printf
#endif

namespace madeline_cube {
namespace {

constexpr float kGroundSkin = 0.01f;

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
    state.wall_normal = wall.normal;
    // Wall contact: detect walls facing any horizontal direction.
    // Use wall_normal.x as primary, wall_normal.z as secondary.
    // The controller uses wall_normal + wall_facing_dot for direction-specific logic,
    // so wall_left/wall_right just gate "there is a wall" entry.
    const float h = std::fabs(wall.normal.x) + std::fabs(wall.normal.z);
    if (h > 0.5f && std::fabs(wall.normal.y) < 0.35f) {
        // Wall is vertical enough (not floor/ceiling) and has horizontal normal component.
        // Determine left/right relative to player facing direction.
        // Cross product of facing with wall normal sign determines side.
        if (wall.normal.x > 0.5f || wall.normal.z > 0.5f) state.wall_left = true;
        if (wall.normal.x < -0.5f || wall.normal.z < -0.5f) state.wall_right = true;
    }
}

GroundHit SweepToGroundHit(const Room& room, const physics::SweepSphereHit& sweep, float max_dist) {
    using namespace physics;
    uint16_t owner_raw = SurfaceOwnerOf(*room.coll_mesh, sweep.face_id);
    int owner_id = -1;
    Vec3 owner_velocity = {};
    if (owner_raw != INVALID_OWNER) {
        owner_id = static_cast<int>(owner_raw);
        const MovingSurface* ms = FindMovingSurface(room, owner_id);
        if (ms) owner_velocity = ms->rider_velocity;
    }
    return GroundHit{
        .hit = true,
        .point = sweep.point,
        .normal = sweep.normal,
        .distance = sweep.t * max_dist,
        .face_id = sweep.face_id,
        .owner_id = owner_id,
        .owner_velocity = owner_velocity,
    };
}

// Returns true if the face permits climbing.
// CollMesh faces require MAT_CLIMBABLE; dynamic colliders are always climbable.
bool FaceIsClimbable(const Room& room, int face_id) {
    if (face_id < 0) return true;  // dynamic collider
    if (!room.coll_mesh) return true;  // no mesh — assume climbable
    if (face_id >= static_cast<int>(room.coll_mesh->header->triangle_count)) return true;  // crafted OOB — safe fallback
    return (room.coll_mesh->triangles[face_id].material & physics::MAT_CLIMBABLE) != 0;
}

GroundHit ProbeFloor(const Room& room, const Vec3& position, float half_height, float probe_distance, float radius) {
    return ProbeFloorDebug(room, position, half_height, probe_distance, radius);
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
    state.wall_climbable = false;
    state.wall_normal = {0.0f, 0.0f, 0.0f};

    MotorResult result;

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
            // Use sphere sweep for floor probe to catch platform edges
            GroundHit floor;
            PM_LOG("[pm] room=%p coll=%p &coll=%p\n",
                   (void*)&room, (void*)room.coll_mesh, (void*)&room.coll_mesh);
            if (room.coll_mesh) {
                using namespace physics;
                const Vec3 probe_origin = {feet_origin.x, feet_origin.y + config_.radius, feet_origin.z};
                SweepSphereHit sweep = SweepSphereMesh(*room.coll_mesh, probe_origin, {0.0f, -1.0f, 0.0f}, config_.radius, probe);
                if (sweep.hit) {
                    floor = SweepToGroundHit(room, sweep, probe);
                } else {
                    floor = QueryFloorSource(room, feet_origin, probe);
                }
            } else {
                floor = QueryFloorSource(room, feet_origin, probe);
            }
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
            state.position.z += wall.normal.z * wall.pushout;
            RemoveIntoNormal(state.velocity, wall.normal);
            RecordWallContact(state, result, wall);
            if (FaceIsClimbable(room, wall.face_id)) state.wall_climbable = true;
        }
    }

    state.grounded = false;
    result.ground_face_id = -1;
    result.ground_normal = {0.0f, 1.0f, 0.0f};

    const GroundHit ground = ProbeFloor(room, state.position, config_.half_height,
                                        config_.ground_contact_tolerance, config_.radius);
    if (ground.hit) {
        ApplyGroundContact(state, result, ground, config_.half_height);
    } else if (grounded_mid_sweep) {
        // Slope-follow: a previous substep grounded, but horizontal substeps
        // walked the player slightly above the slope (the substep ground
        // capture only zeros vertical velocity).  Re-probe with one sweep_step
        // of tolerance so the post-grounding horizontal travel stays pinned.
        const GroundHit follow = ProbeFloor(room, state.position, config_.half_height,
                                            config_.sweep_step + kGroundSkin, config_.radius);
        if (follow.hit) {
            ApplyGroundContact(state, result, follow, config_.half_height);
        }
    } else if (input.wants_ground_snap &&
               was_grounded &&
               state.contact.ground_snap_cooldown_remaining <= 0.0f) {
        const GroundHit snap = ProbeFloor(room, state.position, config_.half_height,
                                          config_.ground_snap_distance, config_.radius);
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
