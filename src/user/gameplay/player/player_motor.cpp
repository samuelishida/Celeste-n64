#include "gameplay/player/player_motor.hpp"

#include <cmath>
#include <cstdio>

#include "gameplay/physics/coll_mesh.hpp"
#include "gameplay/world/world.hpp"



namespace madeline_cube {
namespace {

constexpr float kGroundSkin = 0.01f;
// OG Celeste64 floor/wall split (Player.cs ClimbNormalCheck): surfaces whose
// normal is steeper than this are walls, not floors. Applied to raycast ground
// acceptance so sphere sweeps cannot latch onto vertical faces.
constexpr float kFloorNormalMinY = 0.35f;
// Sphere-sweep floor acceptance gate (Inc 3 climb-and-zoom-fix): a sweep
// sphere overlaps wall faces at t=0 with no distance traveled, so the returned
// centre-relative normal can carry a spurious Y component on diagonal trim
// geometry and falsely ground the player mid-wall. Sweeps require a much
// steeper (≥0.70) normal to be trusted as floor on their own; a sweep with a
// merely-steep (≥0.35) normal is accepted only if a downward raycast confirms
// a real floor at the sweep point. The ray is immune to wall overlap, so it
// disambiguates baked ramps (true faces, ray confirms) from mid-wall diagonal
// hits (no floor below, ray misses → treated as wall).
constexpr float kSweepFloorStrongMinY = 0.70f;

float Length(const Vec3& v) {
    return std::sqrt((v.x * v.x) + (v.y * v.y) + (v.z * v.z));
}

bool IsFloorNormal(const Vec3& n) {
    return n.y >= kFloorNormalMinY;
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

// Full-3D popout vertical cap: diagonal seam planes may carry a small legal Y
// component (ramp walls), but a seam spike must never launch the player.
constexpr float kMaxPopoutY = 6.0f;
// OG Popout parity: velocity is modified only on IMPACT — the first wall
// contact of a tick, or when motion drives more than this many world units
// INTO the wall in one substep. Resting contact never edits velocity
// (device-verified zero-net-force rest behavior).
constexpr float kImpactDriveUnits = 1.0f;

// OG Player.cs:709-712 — remove the full-3D into-plane velocity component
// once, against the true face plane. Tangential speed (including fall speed
// down a flat wall) is preserved exactly; only the into-wall part goes.
void ApplyImpactRemoval(PlayerState& state, const Vec3& n) {
    const float dot = (state.velocity.x * n.x) + (state.velocity.y * n.y) + (state.velocity.z * n.z);
    if (dot >= 0.0f) return;
    state.velocity.x -= n.x * dot;
    state.velocity.y -= n.y * dot;
    state.velocity.z -= n.z * dot;
}

void RecordWallContact(PlayerState& state, MotorResult& result, const WallHit& wall,
                       const Vec3& recorded_normal) {
    result.wall_contact = true;
    result.wall_face_id = wall.face_id;
    result.wall_normal = recorded_normal;
    state.wall_contact = true;
    state.wall_normal = recorded_normal;
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

// Returns true if the face is a death surface (spikes, lava, etc.).
// Only CollMesh faces carry death material; dynamic colliders never kill.
bool FaceIsDeath(const Room& room, int face_id) {
    if (face_id < 0) return false;  // dynamic collider — not death
    if (!room.coll_mesh) return false;  // no mesh — no death zones
    if (face_id >= static_cast<int>(room.coll_mesh->header->triangle_count)) return false;
    return (room.coll_mesh->triangles[face_id].material & physics::MAT_DEATH) != 0;
}

// Returns true if the face is a one-way platform (pass-through from below).
bool FaceIsOneWay(const Room& room, int face_id) {
    if (face_id < 0) return false;  // dynamic collider — never one-way
    if (!room.coll_mesh) return false;
    if (face_id >= static_cast<int>(room.coll_mesh->header->triangle_count)) return false;
    return (room.coll_mesh->triangles[face_id].material & physics::MAT_ONEWAY) != 0;
}

// Returns true if the face is an ice surface (low friction).
bool FaceIsIce(const Room& room, int face_id) {
    if (face_id < 0) return false;  // dynamic collider — never ice
    if (!room.coll_mesh) return false;
    if (face_id >= static_cast<int>(room.coll_mesh->header->triangle_count)) return false;
    return (room.coll_mesh->triangles[face_id].material & physics::MAT_ICE) != 0;
}

// One-way platforms act as floors only when landing from above. Reject a
// one-way floor hit when the player's feet are below the platform surface
// (approaching from below = pass-through). Steep normals are never floor.
bool AcceptFloorHit(const Room& room, const GroundHit& hit, const Vec3& feet_pos) {
    if (!IsFloorNormal(hit.normal)) return false;
    if (!FaceIsOneWay(room, hit.face_id)) return true;
    // Landing from above: feet must be above the platform surface.
    return feet_pos.y > hit.point.y;
}

GroundHit ProbeFloor(const Room& room, const Vec3& position, float half_height, float probe_distance, float radius) {
    return ProbeFloorDebug(room, position, half_height, probe_distance, radius);
}

void ApplyGroundContact(PlayerState& state, MotorResult& result, const Room& room, const GroundHit& hit, float half_height) {
    state.position.y = hit.point.y + half_height;
    if (state.velocity.y < 0.0f) state.velocity.y = 0.0f;
    state.grounded = true;
    state.contact.ground_normal = hit.normal;
    state.contact.coyote_height = state.position.y;
    result.ground_face_id = hit.face_id;
    result.ground_normal = hit.normal;
    result.on_ice = FaceIsIce(room, hit.face_id);
    result.on_oneway = FaceIsOneWay(room, hit.face_id);
    state.on_ice = result.on_ice;
    state.on_oneway = result.on_oneway;
}

void RunWallContact(PlayerState& state, MotorResult& result, const Room& room,
                    float radius, float delta_seconds, bool& contact_this_tick) {
    const WallHit wall = QueryWallNearest(room, state.position, radius);
    if (!wall.hit || FaceIsOneWay(room, wall.face_id)) return;
    // Prefer the owning triangle's plane normal (true face plane); fall back
    // to the centre-relative normal when the query could not supply one.
    const Vec3 pn = Length(wall.plane_normal) > 0.1f ? wall.plane_normal : wall.normal;
    if (wall.pushout > 0.0f) {
        // Full-3D positional popout (OG Popout parity). With true face planes
        // the Y term is ~0 on vertical walls; the cap only bounds diagonal
        // seam/ramp planes.
        float dy = pn.y * wall.pushout;
        if (dy > kMaxPopoutY) dy = kMaxPopoutY;
        if (dy < -kMaxPopoutY) dy = -kMaxPopoutY;
        state.position.x += pn.x * wall.pushout;
        state.position.y += dy;
        state.position.z += pn.z * wall.pushout;
        // Impact-only velocity removal: first contact of the tick, or motion
        // driving meaningfully deeper into the wall this substep. Resting
        // contact leaves velocity untouched (no continuous fall damping).
        const bool edge = !contact_this_tick;
        const float into_units =
            ((state.velocity.x * pn.x) + (state.velocity.y * pn.y) + (state.velocity.z * pn.z)) *
            delta_seconds;
        if (edge || into_units < -kImpactDriveUnits) {
            ApplyImpactRemoval(state, pn);
        }
    }
    RecordWallContact(state, result, wall, pn);
    if (FaceIsDeath(room, wall.face_id)) result.death = true;
}

// Sources the wall_contact FLAG from the plane-based waist probe (the same
// query the climb controller uses), OR'd with the body-center sweep so the
// motor and controller agree on grip. This does NOT move the body — popout is
// handled by RunWallContact at body-center. It is called once per tick (the
// end-of-tick pass) so climb entry sees a stable flag even when the body-center
// sphere would miss a face the waist is gripping.
void RunWallFlagFromWaist(PlayerState& state, const Room& room, float radius,
                          float waist_offset) {
    if (!room.coll_mesh) return;
    const Vec3 waist = {state.position.x,
                        state.position.y + waist_offset,
                        state.position.z};
    const Vec3 into_wall = {-state.last_facing.x, 0.0f, -state.last_facing.z};
    const WallHit gripped = QueryWallClosestToFace(room, waist, radius, into_wall);
    if (gripped.hit && !FaceIsOneWay(room, gripped.face_id)) {
        state.wall_contact = true;
        state.wall_normal = gripped.plane_normal;
    }
}

}  // namespace

PlayerMotor::PlayerMotor(PlayerMotorConfig config) : config_(config) {}

MotorResult PlayerMotor::Step(PlayerState& state, const Room& room, const MotorInput& input, float delta_seconds) const {
    Sanitize(state.position);
    state.velocity = input.requested_velocity;
    Sanitize(state.velocity);

    const bool was_grounded = state.grounded;
    state.contact.was_grounded = was_grounded;
    state.wall_contact = false;
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
    // Impact-gating latch: true once any substep of this tick has recorded a
    // wall contact. Reset every tick so each tick's first contact counts as
    // an impact (OG resolveImpact semantics).
    bool wall_contact_this_tick = false;
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
            if (room.coll_mesh) {
                using namespace physics;
                const Vec3 probe_origin = {feet_origin.x, feet_origin.y + config_.radius, feet_origin.z};
                uint32_t nodes = 0;
                uint32_t* nodes_ptr = room.query_counters ? &nodes : nullptr;
                SweepSphereHit sweep = SweepSphereMesh(*room.coll_mesh, probe_origin, {0.0f, -1.0f, 0.0f}, config_.radius, probe, nodes_ptr);
                if (room.query_counters) {
                    ++room.query_counters->sphere_sweeps;
                    room.query_counters->bvh_nodes_touched += nodes;
                }
                // The sweep sphere overlaps vertical wall faces at t=0 with no
                // distance traveled; only accept strongly floor-like normals
                // outright. A merely-steep normal (≥0.35) is trusted only if a
                // downward raycast (immune to wall overlap) confirms a real
                // floor at the sweep point; otherwise fall back to the
                // (wall-immune) vertical raycast as the floor probe.
                if (sweep.hit && sweep.normal.y >= kSweepFloorStrongMinY) {
                    floor = SweepToGroundHit(room, sweep, probe);
                } else if (sweep.hit &&
                           IsFloorNormal(sweep.normal) &&
                           ConfirmFloorByRay(room, sweep.point, probe)) {
                    floor = SweepToGroundHit(room, sweep, probe);
                } else {
                    floor = QueryFloorSource(room, feet_origin, probe);
                }
            } else {
                floor = QueryFloorSource(room, feet_origin, probe);
            }
            if (floor.hit && AcceptFloorHit(room, floor, feet_origin)) {
                ApplyGroundContact(state, result, room, floor, config_.half_height);
                step_vec.y = 0.0f;
                grounded_mid_sweep = true;
                if (FaceIsDeath(room, floor.face_id)) result.death = true;
            }
        } else if (step_vec.y > 0.0f) {
            const Vec3 head_origin = {state.position.x, prev_head_y, state.position.z};
            const float probe = step_vec.y + kGroundSkin;
            const CeilingHit ceiling = QueryCeilingSource(room, head_origin, probe);
            // One-way platforms are pass-through from below: never a ceiling.
            if (ceiling.hit && !FaceIsOneWay(room, ceiling.face_id)) {
                state.position.y = ceiling.point.y - config_.half_height;
                state.velocity.y = 0.0f;
                step_vec.y = 0.0f;
                if (FaceIsDeath(room, ceiling.face_id)) result.death = true;
            }
        }

        RunWallContact(state, result, room,
                       state.movement_state == PlayerMovementState::Climbing
                           ? config_.climb_collision_radius
                           : config_.radius,
                       delta_seconds, wall_contact_this_tick);
    }

    // Unconditional end-of-tick wall pass: zero-delta ticks (idle/climb hang)
    // skip the substep loop entirely, which used to leave wall flags cleared.
    // Climb uses the climb_collision_radius (3 = OG WallPushoutDist): with the
    // entry anchor placing the waist exactly at wall.Point + normal*3, this is
    // a zero-popout equilibrium AND keeps the flag a true wall normal — must
    // land WITH the unified radius-4 grip probe (Inc 1) or the tangent rest
    // at Normal radius 5 leaves the waist outside probe reach (grab/exit pump).
    RunWallContact(state, result, room,
                   state.movement_state == PlayerMovementState::Climbing
                       ? config_.climb_collision_radius
                       : config_.radius,
                   delta_seconds, wall_contact_this_tick);
    // Waist plane-query flag source (OG parity): the motor and the climb
    // controller must agree on wall contact. This ORs a stable face-plane grip
    // at the waist so climb entry sees wall_contact even where the body-center
    // sphere would miss; it does not move the body (popout already ran above).
    RunWallFlagFromWaist(state, room, config_.radius, config_.wall_probe_center_offset);
    result.wall_contact = state.wall_contact;

    state.grounded = false;
    result.ground_face_id = -1;
    result.ground_normal = {0.0f, 1.0f, 0.0f};
    result.on_ice = false;
    result.on_oneway = false;
    state.on_ice = false;
    state.on_oneway = false;

    const GroundHit ground = ProbeFloor(room, state.position, config_.half_height,
                                        config_.ground_contact_tolerance, config_.radius);
    if (ground.hit && AcceptFloorHit(room, ground, {state.position.x, state.position.y - config_.half_height, state.position.z})) {
        ApplyGroundContact(state, result, room, ground, config_.half_height);
    } else if (grounded_mid_sweep) {
        // Slope-follow: a previous substep grounded, but horizontal substeps
        // walked the player slightly above the slope (the substep ground
        // capture only zeros vertical velocity).  Re-probe with one sweep_step
        // of tolerance so the post-grounding horizontal travel stays pinned.
        const GroundHit follow = ProbeFloor(room, state.position, config_.half_height,
                                            config_.sweep_step + kGroundSkin, config_.radius);
        if (follow.hit && AcceptFloorHit(room, follow, {state.position.x, state.position.y - config_.half_height, state.position.z})) {
            ApplyGroundContact(state, result, room, follow, config_.half_height);
        }
    } else if (input.wants_ground_snap &&
               was_grounded &&
               state.contact.ground_snap_cooldown_remaining <= 0.0f) {
        const GroundHit snap = ProbeFloor(room, state.position, config_.half_height,
                                          config_.ground_snap_distance, config_.radius);
        if (snap.hit && AcceptFloorHit(room, snap, {state.position.x, state.position.y - config_.half_height, state.position.z})) {
            ApplyGroundContact(state, result, room, snap, config_.half_height);
        }
    }

    if (state.velocity.y > 0.0f) {
        const Vec3 head = {state.position.x, state.position.y + config_.half_height, state.position.z};
        const CeilingHit ceiling = QueryCeilingSource(room, head, config_.ground_contact_tolerance);
        if (ceiling.hit && !FaceIsOneWay(room, ceiling.face_id)) {
            state.position.y = ceiling.point.y - config_.half_height;
            state.velocity.y = 0.0f;
            if (FaceIsDeath(room, ceiling.face_id)) result.death = true;
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
