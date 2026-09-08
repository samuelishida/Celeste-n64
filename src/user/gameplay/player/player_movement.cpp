#include "gameplay/player/player_movement.hpp"

#include <algorithm>
#include <cmath>

#include "gameplay/physics/coll_mesh.hpp"
#include "gameplay/physics/geom.hpp"

namespace madeline_cube {

namespace {

using namespace physics;

float Length3(const Vec3& v) {
    return std::sqrt((v.x * v.x) + (v.y * v.y) + (v.z * v.z));
}

// OG FloorNormalCheck (Player.cs:886-887): a floor normal has a +Y component
// and is not a climb-normal (near-vertical). |n.y| >= 0.35 (OG ClimbNormalCheck
// negation). Demake already applies >= 0.35 in the motor; this is the OG gate.
bool IsFloorNormalOG(const Vec3& n) {
    return std::fabs(n.y) >= 0.35f && n.y > 0.0f;
}

// One-way platforms are pass-through from below; never a floor/ceiling when
// approaching from below (demake behavior).
bool IsOneWay(const Room& room, int face_id) {
    if (face_id < 0) return false;
    if (!room.coll_mesh) return false;
    if (face_id >= static_cast<int>(room.coll_mesh->header->triangle_count)) return false;
    return (room.coll_mesh->triangles[face_id].material & physics::MAT_ONEWAY) != 0;
}

// True if the face is a death surface (spikes, lava). Only CollMesh faces
// carry death material; dynamic colliders never kill.
bool IsDeathFace(const Room& room, int face_id) {
    if (face_id < 0) return false;
    if (!room.coll_mesh) return false;
    if (face_id >= static_cast<int>(room.coll_mesh->header->triangle_count)) return false;
    return (room.coll_mesh->triangles[face_id].material & physics::MAT_DEATH) != 0;
}

// Surface material flags carried on contact (S6): the old motor set on_ice /
// on_oneway from the repaired face; the single mover owns that job now.
void RecordSurfaceFlags(const Room& room, int face_id, PlayerState& state) {
    state.on_ice = false;
    state.on_oneway = IsOneWay(room, face_id);
    if (face_id < 0 || !room.coll_mesh) return;
    if (face_id >= static_cast<int>(room.coll_mesh->header->triangle_count)) return;
    state.on_ice = (room.coll_mesh->triangles[face_id].material & physics::MAT_ICE) != 0;
}

}  // namespace

MoveService::MoveService(MoveServiceConfig config) : config_(config) {}

Vec3 MoveService::SolidWaistTestPos(const PlayerState& state) const {
    return {state.position.x, state.position.y - config_.half_height + 3.0f, state.position.z};
}

Vec3 MoveService::SolidHeadTestPos(const PlayerState& state) const {
    return {state.position.x, state.position.y - config_.half_height + 10.0f, state.position.z};
}

Vec3 MoveService::SolidFeetPos(const PlayerState& state) const {
    return {state.position.x, state.position.y - config_.half_height, state.position.z};
}

bool MoveService::GroundCheck(const PlayerState& state, const Room& room,
                              Vec3* pushout, Vec3* normal) const {
    const Vec3 origin = SolidFeetPos(state);
    Vec3 ray_origin = {origin.x, origin.y + config_.ground_ray_up, origin.z};
    const GroundHit hit = QueryFloorSource(room, ray_origin, config_.ground_ray_len);
    if (!hit.hit || !IsFloorNormalOG(hit.normal)) return false;
    // One-way platforms ground only when the feet are AT or above the surface
    // (landing from above). Strict `origin.y < hit.point.y` = feet strictly
    // below = pass-through from below (never a floor).
    if (IsOneWay(room, hit.face_id) && origin.y < hit.point.y) return false;
    // pushout carries `position` (the CENTER) to land the FEET on the hit:
    // center' = hit.point + half_height, so pushout = center' - position.
    if (pushout) *pushout = {hit.point.x - state.position.x,
                             (hit.point.y + config_.half_height) - state.position.y,
                             hit.point.z - state.position.z};
    if (normal) *normal = hit.normal;
    return true;
}

bool MoveService::CeilingCheck(const PlayerState& state, const Room& room,
                               Vec3* hit_out) const {
    const Vec3 origin = SolidFeetPos(state);
    Vec3 ray_origin = {origin.x, origin.y + config_.ceiling_probe_up, origin.z};
    const CeilingHit hit = QueryCeilingSource(room, ray_origin, config_.ceiling_height - 1.0f);
    if (!hit.hit) return false;
    // One-way platforms are pass-through from below: never a ceiling when the
    // feet are strictly below the surface. Only blocks from underneath when
    // the feet are ABOVE the surface (a one-way above the head).
    if (IsOneWay(room, hit.face_id) && origin.y < hit.point.y) return false;
    if (hit_out) *hit_out = hit.point;
    return true;
}

bool MoveService::Popout(PlayerState& state, const Room& room, bool resolve_impact) const {
    // Death flag is per-contact: cleared here, set if ANY repaired contact face
    // carries MAT_DEATH (the old motor's FaceIsDeath sites, now mover-owned).
    state.death_contact = false;
    // ground test
    Vec3 pushout{};
    Vec3 normal{};
    Vec3 hit_point{};
    int ground_face = -1;
    int ceiling_face = -1;
    if (GroundCheck(state, room, &pushout, &normal)) {
        state.position.x += pushout.x;
        state.position.y += pushout.y;
        state.position.z += pushout.z;
        state.grounded = true;
        state.contact.ground_normal = normal;
        // Surface flags + death material from the repaired floor face.
        {
            const Vec3 origin = SolidFeetPos(state);
            const Vec3 ray_origin = {origin.x, origin.y + config_.ground_ray_up, origin.z};
            const GroundHit face = QueryFloorSource(room, ray_origin, config_.ground_ray_len);
            if (face.hit) {
                RecordSurfaceFlags(room, face.face_id, state);
                if (IsDeathFace(room, face.face_id)) state.death_contact = true;
            }
        }
        if (resolve_impact && state.velocity.y < 0.0f)
            state.velocity.y = 0.0f;  // OG velocity.Z = Max(velocity.Z, 0)
    }

    // ceiling test — independent of ground (OG else-if on ground only).
    else if (CeilingCheck(state, room, &hit_point)) {
        state.position.y += (hit_point.y - config_.ceiling_height) - state.position.y;
        if (resolve_impact && state.velocity.y > 0.0f)
            state.velocity.y = 0.0f;  // OG velocity.Z = Min(velocity.Z, 0)
    }
    // wall test — a SEPARATE independent if (Player.cs:688-714). Not in the
    // ground/ceiling else-chain: a grounded player walking into a wall must
    // still be popped out along the wall normal.
    const WallHit waist = QueryWallPushout(room, SolidWaistTestPos(state), config_.wall_radius);
    const WallHit head = QueryWallPushout(room, SolidHeadTestPos(state), config_.wall_radius);
    const WallHit* wall = nullptr;
    if (waist.hit && head.hit)
        wall = head.pushout > waist.pushout ? &head : &waist;
    else if (waist.hit) wall = &waist;
    else if (head.hit) wall = &head;

    if (wall) {
        state.position.x += wall->pushout * wall->plane_normal.x;
        state.position.y += wall->pushout * wall->plane_normal.y;
        state.position.z += wall->pushout * wall->plane_normal.z;

        if (resolve_impact) {
            const Vec3 vnorm = physics::Norm(state.velocity);
            const float dot = std::min(0.0f, physics::Dot(vnorm, wall->plane_normal));
            const float len = Length3(state.velocity);
            state.velocity.x -= wall->plane_normal.x * len * dot;
            state.velocity.y -= wall->plane_normal.y * len * dot;
            state.velocity.z -= wall->plane_normal.z * len * dot;
        }
        state.wall_contact = true;
        state.wall_normal = wall->plane_normal;
        if (IsDeathFace(room, wall->face_id)) state.death_contact = true;
        return true;
    }

    // A ground or ceiling repair happened but no wall. The return value IS the
    // wall-only signal (OG "Returns true if popped out of a wall"), which the
    // SweepMove latch uses to apply resolveImpact once. Ground/ceiling don't
    // trip it.
    return false;
}

void MoveService::SweepMove(PlayerState& state, const Room& room, const Vec3& amount,
                            bool resolve_impact) const {
    if (Length3(amount) <= 0.0001f) {
        // Zero movement: still run the unconditional idle popout (OG calls
        // SweepTestMove then a separate Popout(false) "for good measure" every
        // tick — Player.cs:358-373). Keeps contact flags alive on idle ticks.
        Popout(state, room, false);
        return;
    }

    const float remaining = Length3(amount);
    const Vec3 step_normal = {amount.x / remaining, amount.y / remaining, amount.z / remaining};
    float left = remaining;

    while (left > 0.0f) {
        const float step = std::min(left, config_.substep);
        left -= step;
        state.position.x += step_normal.x * step;
        state.position.y += step_normal.y * step;
        state.position.z += step_normal.z * step;

        if (Popout(state, room, resolve_impact)) {
            // Only the first wall impact per move resolves (OG resolveImpact=false).
            resolve_impact = false;
        }
    }

    // One unconditional idle popout for good measure (Player.cs:368-373).
    Popout(state, room, false);
}

void MoveService::SweepMoveClimb(PlayerState& state, const Room& room,
                                  const Vec3& amount) const {
    // Climb movement (climb-bug fix): OG climb uses SweepTestMove(move, false)
    // — a collision-aware sweep along the wall plane. The full Popout is NOT
    // used: its head wall query fires on sloped walls (the head is 10 above
    // the center and sits at a different distance from a sloped plane, so the
    // head popout fires every frame with a full-3D push including Y →
    // down-push + wiggle + ejection), and its ceiling check fights the climb
    // up into a ledge. Waist-only wall popout keeps the body out of the wall
    // (the waist is at the anchor, pushout ≈ 0) without fighting the climb.
    if (Length3(amount) <= 0.0001f) return;
    const float remaining = Length3(amount);
    const Vec3 step_normal = {amount.x / remaining, amount.y / remaining,
                              amount.z / remaining};
    float left = remaining;
    while (left > 0.0f) {
        const float step = std::min(left, config_.substep);
        left -= step;
        state.position.x += step_normal.x * step;
        state.position.y += step_normal.y * step;
        state.position.z += step_normal.z * step;
        const WallHit waist = QueryWallPushout(room, SolidWaistTestPos(state),
                                               config_.wall_radius);
        if (waist.hit) {
            state.position.x += waist.pushout * waist.plane_normal.x;
            state.position.y += waist.pushout * waist.plane_normal.y;
            state.position.z += waist.pushout * waist.plane_normal.z;
        }
    }
}

void MoveService::GroundSnap(PlayerState& state, const Room& room, float dt) const {
    if (state.contact.ground_snap_cooldown_remaining > 0.0f) return;
    if (!state.contact.was_grounded || state.grounded) return;

    const Vec3 origin = SolidFeetPos(state);
    const GroundHit hit = QueryFloorSource(room, origin, config_.ground_snap_distance);
    if (hit.hit && IsFloorNormalOG(hit.normal)) {
        state.position.y = hit.point.y + config_.half_height;
        // Surface flags from the snapped face (S6).
        RecordSurfaceFlags(room, hit.face_id, state);
        // Refresh grounded state via a GroundCheck so normal/coyote follow.
        Vec3 normal{};
        if (GroundCheck(state, room, nullptr, &normal)) {
            state.contact.ground_normal = normal;
        }
    }
}

void MoveService::CancelGroundSnap(PlayerState& state) const {
    // OG Player.cs:722-723 — called at every takeoff site (Jump, WallJump,
    // dash, climb) so the 5-unit snap never eats a launch. The single mover
    // owns the same contract: state code cancels, the mover gates the snap.
    state.contact.ground_snap_cooldown_remaining = config_.ground_snap_cooldown;
}

}  // namespace madeline_cube
