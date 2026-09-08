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
// staying equal to MoveServiceConfig::half_height, both defaulted to 10.

// Grip-grace window (climb-and-zoom-fix): how long the climb holds through a

// Grip-grace window (climb-and-zoom-fix): how long the climb holds through a
// momentary grip-probe miss before releasing. The sphere probe flickers on
// real geometry (seams, corners, tangent boundary), which used to drop the
// grab for a frame and pump a release/fall/regrab cycle while climbing up.
// 0.12s = ~7 ticks, long enough to ride out a single probe flicker without
// feeling laggy on a deliberate release.
constexpr float kClimbGripGrace = 0.12f;

// Mantle persistence: consecutive ticks the ledge probe must stay clear before
// the mantle hop fires. At climb_speed 40 u/s and 60 fps, 2 ticks ≈ 1.3 units
// — enough to reject a single-frame tangency flicker while still mantling
// promptly at the real lip.
constexpr int kClimbMantleMinTicks = 2;

float Length(float x, float y) {
    return std::sqrt((x * x) + (y * y));
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
                            const Room* room, float y_extra = 0.0f) {
    if (room == nullptr) return WallHit{};
    const Vec3 into_wall = {-state.last_facing.x, 0.0f, -state.last_facing.z};
    return QueryClimbWall(*room, ClimbWaistPoint(state, config, y_extra),
                          config.climb_check_dist, into_wall);
}

// Ledge vertex support probe (climb-parity-3 Inc 1 stopgap for the lip):
// the plane query projects onto the face's INFINITE plane, so a probe
// hovering above a wall's top edge still "hits" (at an extension of the face
// plane). OG's ClimbCheckAt miss is against actual geometry — a face ENDS at
// its triangle vertices. This helper bounds the gripped-face hit to its
// triangle: the probe counts as "supported" only if the hit point at its own
// height lies inside one of the matching wall faces' vertex extremes within
// a small tangent skin. A wall that genuinely continues above reports
// supported (no mantle); a wall that ends reports unsupported → mantle.
// Inc 4 replaces the mantle gate wholesale with the OG ClimbCheckAt miss via
// the same bounding (this helper is written to migrate directly).
bool LedgeHopMiss(const PlayerState& state, const MovementConfig& config,
                  const Room* room, const WallHit& gripped, const Vec3& climb_normal,
                  const PlayerInput& input) {
    (void)climb_normal;
    // Only holding up can ever mantle; every other case reports "no miss" so
    // downstream flicker guards are untouched.
    if (!(input.climb_held && input.move.y > 0.3f)) return false;
    if (room == nullptr || !room->coll_mesh || !gripped.hit) return false;

    const int tri_count = static_cast<int>(room->coll_mesh->header->triangle_count);
    if (gripped.face_id < 0 || gripped.face_id >= tri_count) return false;

    // The lip probe point: waist + climb_ledge_probe_extra.
    const Vec3 probe = ClimbWaistPoint(state, config, config.climb_ledge_probe_extra);

    // Scan faces whose plane normal matches the gripped face's (wall identity
    // across bake seams): the probe is supported when it lies WITHIN a
    // matching face's vertex extents (in its own plane) at the probe height.
    const Vec3& n = gripped.plane_normal;
    for (int f = 0; f < tri_count; ++f) {
        const physics::CollTriangle& ct = room->coll_mesh->triangles[f];
        if (!(ct.material & physics::MAT_SOLID)) continue;
        const Vec3& a = room->coll_mesh->world_verts[ct.i0];
        const Vec3& b = room->coll_mesh->world_verts[ct.i1];
        const Vec3& c = room->coll_mesh->world_verts[ct.i2];
        Vec3 fn = physics::Norm(physics::Cross(physics::Sub(b, a),
                                               physics::Sub(c, a)));
        // Orient toward the probe side; skip faces not facing the player.
        const Vec3 to_probe = physics::Sub(probe, a);
        if (physics::Dot(fn, to_probe) < 0.0f) fn = {-fn.x, -fn.y, -fn.z};
        if (std::fabs(fn.y) >= 0.35f) continue;              // not a wall
        if (physics::Dot(fn, n) < 0.7f) continue;            // not the same wall family
        // Inside the face's vertex extents, evaluated ON THE FACE PLANE?
        // A vertical wall triangle is zero-thick along its own normal, so the
        // raw probe point (3 units off the plane) always fails an AABB test
        // along that axis. Project the probe onto the plane first; the AABB
        // check on the normal axis then always passes for the projected point
        // and the tangent axes decide support.
        const float plane_dist = std::fabs(physics::Dot(fn, to_probe));
        if (plane_dist > config.climb_check_dist) continue;
        const Vec3 proj = physics::Sub(probe, {fn.x * physics::Dot(fn, to_probe),
                                               fn.y * physics::Dot(fn, to_probe),
                                               fn.z * physics::Dot(fn, to_probe)});
        Vec3 vmin = {std::min(a.x, std::min(b.x, c.x)),
                     std::min(a.y, std::min(b.y, c.y)),
                     std::min(a.z, std::min(b.z, c.z))};
        Vec3 vmax = {std::max(a.x, std::max(b.x, c.x)),
                     std::max(a.y, std::max(b.y, c.y)),
                     std::max(a.z, std::max(b.z, c.z))};
        if (proj.x < vmin.x - 0.01f || proj.x > vmax.x + 0.01f) continue;
        if (proj.y < vmin.y - 0.01f || proj.y > vmax.y + 0.01f) continue;
        if (proj.z < vmin.z - 0.01f || proj.z > vmax.z + 0.01f) continue;
        return false;  // some face of the same wall supports the probe: NO miss
    }
    return true;  // no supporting wall at the probe point: LEDGE MISSES
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
    state.position = {
        hit.point.x + n.x * config.climb_pushout_dist,
        hit.point.y + (config.player_half_height - config.climb_waist_offset),
        hit.point.z + n.z * config.climb_pushout_dist,
    };
    state.target_facing = NormalizeXZ({-n.x, 0.0f, -n.z}, state.target_facing);
    state.last_facing = state.target_facing;
}

// 1. Inner corner (OG :1567-1574): pressing sideways into a joining wall —
// a side ray at the waist finds the new face and snaps her onto it. OG's
// move_x sign picks the BODY-RELATIVE side: sideways = the wall-tangent
// direction of the player's intent, i.e. the component of the move input
// PERPENDICULAR to the wall normal (XZ-projected), not a world-axis ray.
bool InnerCornerSideRay(PlayerState& state, const MovementConfig& config,
                        const Room* room, const Vec3& climb_normal,
                        const Vec3& move_xz) {
    if (room == nullptr) return false;
    // Side direction = move intent projected onto the wall tangent plane,
    // normalized (zero when the intent is purely up/into the wall). The
    // projection length is checked BEFORE normalization — NormalizeXZ of a
    // tiny vector (e.g. a plain up-climb's camera-relative XZ) produces a
    // unit vector that would pass a post-normalization length check and fire
    // the corner ray (climb-bug probe: outer corner teleports the body).
    const float proj_x = move_xz.x + climb_normal.x * (move_xz.x * climb_normal.x + move_xz.z * climb_normal.z) * -1.0f;
    const float proj_z = move_xz.z + climb_normal.z * (move_xz.x * climb_normal.x + move_xz.z * climb_normal.z) * -1.0f;
    const float proj_len = std::sqrt((proj_x * proj_x) + (proj_z * proj_z));
    if (proj_len < 0.1f) return false;  // no sideways component
    const Vec3 tangent = {proj_x / proj_len, 0.0f, proj_z / proj_len};
    
    WallHit out{};
    QueryWallRay(*room, ClimbWaistPoint(state, config),
                 {tangent.x, 0.0f, tangent.z}, config.climb_check_dist, &out);
    if (!out.hit) return false;
    // Only adopt when the ray found a DIFFERENT wall than the gripped one
    // (a side ray along her own wall hits the same wall — that's movement,
    // not a corner).
    if (DotXZ(out.plane_normal, state.climb.wall_normal) > 0.5f) return false;
    AdoptCornerWall(state, config, out);
    return true;
}

// 2. Slope-away snap (OG :1576-1583): raycast back into the wall along
// -climb_normal keeps the grip glued to walls that lean away. Skipped when
// the grip probe already reports the same face this tick (CONSIDER adopted).
bool SlopeSnapRay(PlayerState& state, const MovementConfig& config,
                  const Room* room, const Vec3& climb_normal) {
    if (room == nullptr) return false;
    WallHit out{};
    QueryWallRay(*room, ClimbWaistPoint(state, config),
                 {-climb_normal.x, 0.0f, -climb_normal.z},
                 config.climb_check_dist + 2.0f, &out);
    if (!out.hit) return false;
    AdoptCornerWall(state, config, out);
    return true;
}

// 3. Outer corner rotation (OG :1585-1601): climbing up/over a convex edge —
// forward+side raycast finds the new wall; the adopt starts the 0.2 s ease.
bool OuterCornerRay(PlayerState& state, const MovementConfig& config,
                    const Room* room, const Vec3& climb_normal,
                    const Vec3& move_xz, const WallHit& gripped) {
    if (room == nullptr) return false;
    (void)gripped;
    const Vec3 fwd = NormalizeXZ({-climb_normal.x, 0.0f, -climb_normal.z});
    // Body-relative side (same tangent projection as InnerCornerSideRay).
    const float into = move_xz.x * climb_normal.x + move_xz.z * climb_normal.z;
    // Body-relative side (same tangent projection as InnerCornerSideRay).
    // Projection length checked BEFORE normalization (see InnerCornerSideRay).
    const float proj_x = move_xz.x - climb_normal.x * into;
    const float proj_z = move_xz.z - climb_normal.z * into;
    const float proj_len = std::sqrt((proj_x * proj_x) + (proj_z * proj_z));
    if (proj_len < 0.1f) return false;
    const Vec3 side = {proj_x / proj_len, 0.0f, proj_z / proj_len};
    const Vec3 waist = ClimbWaistPoint(state, config);
    const Vec3 origin = {
        waist.x + fwd.x * (config.climb_check_dist + 1.0f) + side.x,
        waist.y,
        waist.z + fwd.z * (config.climb_check_dist + 1.0f) + side.z,
    };
    WallHit out{};
    QueryWallRay(*room, origin, {-side.x, 0.0f, -side.z}, config.climb_check_dist * 2.0f, &out);
    if (!out.hit) return false;
    if (DotXZ(out.plane_normal, state.climb.wall_normal) > 0.5f) return false;
    AdoptCornerWall(state, config, out);
    return true;
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
    // OG CancelGroundSnap parity (Player.cs:742): a takeoff must never be
    // eaten by the 5-unit ground snap on the next ticks.
    state.contact.ground_snap_cooldown_remaining = config.ground_snap_cooldown;
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
}

void ApplyJumpAndGravity(
    PlayerState& state,
    const PlayerInput& input,
    const Vec3& move_input,
    const MovementConfig& config,
    float delta_seconds
) {
    const bool can_jump = state.grounded || state.coyote_time_remaining > 0.0f;
    // Inc 1: the input device owns the 0.10 s press buffer — `input.jump_pressed`
    // is the device-buffered edge (live for the buffer window). Consume it on
    // acceptance so the buffered edge cannot re-fire next frame.
    if (input.jump_pressed && can_jump) {
        input.ConsumeJumpPress();
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

}  // namespace

PlayerController::PlayerController(MovementConfig config) : config_(config), mover_() {
    // og-mover-overhaul Inc 2: the parity warning against PlayerMotorConfig is
    // gone (PlayerMotor is deleted). The single mover is MoveService (Inc 1);
    // MoveServiceConfig::half_height stays default 10 == config_.player_half_height.
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

    // Full OG pipeline (Inc 2): timers/input -> state (velocity edits) ->
    // MovePhase (SweepMove + LateContact). MovePhase(nullptr) already routes
    // to the contact-snapshot-only path, so the room-less fixture case and
    // the full pipeline share one body (review C7: no dual branch).
    StepContext context = TimerInputPhase(state, input, camera_forward, delta_seconds);
    StatePhase(state, input, context, delta_seconds, room);
    MovePhase(state, delta_seconds, room);
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
        state.coyote_time_remaining = config_.coyote_time;
        state.contact.coyote_height = state.position.y;  // Record Y for coyote-jump snap
        context.dash_momentum = {};
        if (state.dash_reset_cooldown_remaining <= 0.0f) {
            state.air_dash_available = true;
        }
    } else {
        state.coyote_time_remaining = Approach(state.coyote_time_remaining, 0.0f, delta_seconds);
    }

    // Inc 1: jump/dash press buffering moved to the input device (OG
    // VirtualButton parity) — `input.jump_pressed`/`input.dash_pressed` are the
    // device-buffered edges. The controller no longer owns buffer timers.

    state.dash_cooldown_remaining = Approach(state.dash_cooldown_remaining, 0.0f, delta_seconds);
    state.dash_reset_cooldown_remaining = Approach(state.dash_reset_cooldown_remaining, 0.0f, delta_seconds);
    state.no_dash_jump_remaining = Approach(state.no_dash_jump_remaining, 0.0f, delta_seconds);
    state.no_skid_jump_remaining = Approach(state.no_skid_jump_remaining, 0.0f, delta_seconds);
    state.climb.cooldown_remaining = Approach(state.climb.cooldown_remaining, 0.0f, delta_seconds);
    state.no_move_time_remaining = Approach(state.no_move_time_remaining, 0.0f, delta_seconds);
    state.contact.ground_snap_cooldown_remaining = Approach(
        state.contact.ground_snap_cooldown_remaining,
        0.0f,
        delta_seconds
    );
    state.platform_carry.time_remaining = Approach(state.platform_carry.time_remaining, 0.0f, delta_seconds);

    return context;
}

void PlayerController::StClimbingUpdate(
    PlayerState& state,
    const PlayerInput& input,
    StepContext& context,
    float delta_seconds,
    const Room* room
) const {
    const Vec3& move_input = context.move_input;
    const bool has_move_input = context.has_move_input;
    // Climbing — OG Celeste64 model (Player.cs StClimbingUpdate): unlimited
    // grip while a player holds Climb on a wall; gravity-free kinematic
    // movement on the wall plane. No stamina.
        // Persistent wall identity (OG climbingWallNormal/climbingWallActor):
        // the grip belongs to the face the player GRABBED, not whichever face
        // is nearest this tick. The plane-based face query returns a stable
        // face normal (no seam diagonal), and we keep the stored face unless a
        // genuinely different wall is reached. This eliminates the frame-to-
        // frame normal flip that pumped the release check.
        const Vec3 into_wall = {-state.last_facing.x, 0.0f, -state.last_facing.z};
        Vec3 climb_normal = state.climb.wall_normal;
        if (LengthXZ(climb_normal) < 0.1f) {
            climb_normal = NormalizeXZ(into_wall);
        }

        // Grip presence: the unified ClimbCheckAt waist query (feet+3, radius
        // 4, plane-based, OG ClimbNormalCheck gate). Without a room
        // (controller-only fixtures) fall back to the motor's body-center flag.
        const WallHit gripped = ClimbCheckAt(state, config_, room);
        const bool grip_present = room != nullptr ? gripped.hit : state.wall_contact;

        // Re-acquire the gripped face by identity, not by nearest-any-wall.
        // Keep the stored normal while the same face (or a consistent normal)
        // is present; update only on a genuine wall change (different face and
        // divergent normal, e.g. rounding a corner).
        if (gripped.hit) {
            const bool same_face = state.climb.grip_face_id == gripped.face_id;
            const bool climb_normal_xz = LengthXZ(climb_normal) > 0.1f;
            const bool same_direction = climb_normal_xz &&
                DotXZ(gripped.plane_normal, climb_normal) > 0.5f;
            if (!climb_normal_xz) {
                // Degenerate stored normal (e.g. a Y-normal from a sphere
                // query): adopt the gripped face's plane normal.
                state.climb.wall_normal = gripped.plane_normal;
            } else if (same_face || same_direction) {
                // Still on the gripped wall (or an equivalent face): keep normal.
                if (LengthXZ(state.climb.wall_normal) < 0.1f) {
                    state.climb.wall_normal = gripped.plane_normal;
                }
            } else {
                // Reached a genuinely different wall: adopt it.
                state.climb.wall_normal = gripped.plane_normal;
            }
            state.climb.grip_face_id = gripped.face_id;
            climb_normal = state.climb.wall_normal;
        }

        const float wall_facing_dot = DotXZ(climb_normal, into_wall);

        // Ledge-hop mantle (OG Player.cs:1602-1614): holding up while the lip
        // probe (waist + 1 unit, OG ClimbCheckAt(+1 UnitZ)) MISSES means the
        // feet have effectively reached the top. The probe is the SAME unified
        // plane query as grip (one query family for all climb decisions, inc 1
        // decision). OG's miss is against actual geometry — the plane query
        // projects onto the face's INFINITE plane, so a lip probe beside an
        // open vertical edge (a triangle boundary, not geometry) would still
        // "hit" unless caller-bounded. LedgeHopMiss enforces that OG
        // geometry-bounded miss: the hit's plane-point must carry real wall
        // support at the probe's height on the gripped face (or any face
        // sharing its normal) between the triple's vertex extremes.
        // Mantle persistence: the probe can flicker at the tangency boundary
        // mid-wall (real-geometry pump while climbing up). Require the ledge
        // to be clear for several consecutive ticks before launching so a
        // single-frame flicker can't eject her. The counter resets whenever
        // the lip probe reports supported again (the else branch below) —
        // that is the probe-agreement guard: while the lip still reads as a
        // wall, the top has not been reached. (A raw grip-probe reset is NOT
        // used: the plane grip query hits the infinite plane even above the
        // wall top, so it would suppress the mantle at the real lip.)
        if (input.climb_held &&
            input.move.y > 0.3f &&
            room != nullptr &&
            LedgeHopMiss(state, config_, room, gripped, climb_normal, input)) {
            ++state.climb.ledge_miss_ticks;
            if (state.climb.ledge_miss_ticks >= kClimbMantleMinTicks) {
                const Vec3 hop_dir = LengthXZ(state.climb.wall_normal) > 0.1f
                    ? NormalizeXZ(state.climb.wall_normal)
                    : NormalizeXZ(climb_normal);
                state.movement_state = PlayerMovementState::Normal;
                state.locomotion_state = LocomotionState::Jump;
                // Facing stays into the old wall; forward = over the lip (-normal).
                state.velocity.x = -hop_dir.x * config_.climb_hop_forward_speed;
                state.velocity.y = config_.climb_hop_up_speed;
                state.velocity.z = -hop_dir.z * config_.climb_hop_forward_speed;
                state.no_move_time_remaining = config_.climb_hop_no_move_time;
                state.climb.cooldown_remaining = config_.climb_cooldown;
                // OG CancelGroundSnap parity (Player.cs:1473 climb exit): the
                // mantle hop must not be snap-pulled back to the wall/floor.
                state.contact.ground_snap_cooldown_remaining = config_.ground_snap_cooldown;
                context.mantle_triggered = true;
                return;
            }
        } else {
            state.climb.ledge_miss_ticks = 0;
        }

        // Release conditions: let go of grab, lost the wall, or faced away.
        // No cooldown here (OG parity): only the mantle sets one, so hanging
        // at a wall top cannot pump into release/fall/regrab cycles.
        //
        // Grip-grace: the sphere grip probe can flicker for a frame on real
        // geometry (seams, corners, floating-point at the tangent boundary),
        // which pumped the player into a release/fall/regrab cycle mid-wall
        // while climbing up. A momentary probe miss must NOT drop the grab —
        // only release after grip stays absent for the full grace window.
        // Button release and facing-away are still instant (deliberate intent,
        // not probe noise). The mantle path (above) already handles the real
        // wall top before this guard.
        if (!input.climb_held || wall_facing_dot < 0.5f) {
#ifdef N64
            debugf("[climb] EXIT %s lf=(%.2f,%.2f) wn=(%.2f,%.2f) grip=%d\n",
                   !input.climb_held ? "button" : "facing",
                   (double)state.last_facing.x, (double)state.last_facing.z,
                   (double)climb_normal.x, (double)climb_normal.z,
                   (int)grip_present);
#endif
            (void)climb_normal; (void)grip_present;  // referenced by the log above on N64
            state.movement_state = PlayerMovementState::Normal;
            state.locomotion_state = state.grounded ? LocomotionState::Idle
                                                    : LocomotionState::Fall;
            return;
        }
        if (grip_present) {
            state.climb.grip_lost_timer = kClimbGripGrace;
        } else {
            state.climb.grip_lost_timer -= delta_seconds;
            if (state.climb.grip_lost_timer <= 0.0f) {
#ifdef N64
                debugf("[climb] release grip_lost (grip=%d lf=(%.2f,%.2f) wn=(%.2f,%.2f))\n",
                       (int)grip_present,
                       (double)state.last_facing.x, (double)state.last_facing.z,
                       (double)climb_normal.x, (double)climb_normal.z);
#endif
                (void)climb_normal;  // referenced by the log above on N64
                state.movement_state = PlayerMovementState::Normal;
                state.locomotion_state = state.grounded ? LocomotionState::Idle
                                                        : LocomotionState::Fall;
                return;
            }
        }

        // Wall jump from climb: velocity is SET away from the wall (OG WallJump).
        if (input.jump_pressed) {
            input.ConsumeJumpPress();
            state.movement_state = PlayerMovementState::Normal;
            const Vec3 jump_dir = NormalizeXZ(climb_normal,
                Vec3{-state.last_facing.x, 0.0f, -state.last_facing.z});
            // Neutral wall jump splits vertical/horizontal (demake-tuned
            // neutral pair 105/40, formerly MovementProfile; directional uses
            // the OG pair 90/83.2). Config single source (S4).
            const bool neutral = !has_move_input;
            const float horizontal = neutral
                ? config_.neutral_wall_jump_horizontal_speed
                : config_.wall_jump_speed_x;
            const float vertical = neutral
                ? config_.neutral_wall_jump_vertical_speed
                : config_.wall_jump_speed_y;
            state.velocity.x = jump_dir.x * horizontal;
            state.velocity.y = vertical;
            state.velocity.z = jump_dir.z * horizontal;
            state.target_facing = jump_dir;
            state.last_facing = jump_dir;
            state.locomotion_state = LocomotionState::Jump;
            state.hold_jump_speed = config_.jump_speed;
            state.hold_jump_time_remaining = config_.jump_hold_time;
            // OG CancelGroundSnap parity (launch site, Player.cs:758).
            state.contact.ground_snap_cooldown_remaining = config_.ground_snap_cooldown;
            // No climb cooldown (OG parity): the facing-away gate blocks
            // instant re-grab after the launch.
            return;
        }

        // Dash off the wall (parity with OG dash-out of Climb). The device
        // owns the 0.10 s press buffer, so `input.dash_pressed` is the
        // buffered edge; the press edge is consumed on acceptance.
        const bool climb_dash_want = input.dash_pressed;
        if (climb_dash_want &&
            state.air_dash_available &&
            state.dash_cooldown_remaining <= 0.0f) {
            if (has_move_input) {
                state.target_facing = move_input;
            }
            state.target_facing = NormalizeXZ(state.target_facing, climb_normal);
            state.last_facing = state.target_facing;
            state.dashed_on_ground = false;
            state.movement_state = PlayerMovementState::Dashing;
            state.locomotion_state = LocomotionState::Dash;
            state.air_dash_available = false;
            state.auto_jump = false;
            state.dash_hitstop_remaining = config_.dash_hitstop_time;
            state.dash_active_remaining = config_.dash_active_time;
            state.dash_time_remaining = config_.dash_hitstop_time + config_.dash_active_time;
            state.dash_reset_cooldown_remaining = config_.dash_reset_cooldown;
            state.no_dash_jump_remaining = config_.no_dash_jump_time;
            input.ConsumeDashPress();
            context.dash_momentum = {};
            state.velocity = {};
            return;
        }

        // ── Corner ease integration (OG structure: the ease branch REPLACES
        // the movement branch). Runs before release checks? No — release +
        // mantle + walljump are handled above (release always wins, OG checks
        // Climb.Down first at :1451). When easing: zero velocity, lerp the
        // position + facing, advance the timer, and SKIP wall movement.
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
            state.climb.corner_ease = Approach(
                state.climb.corner_ease, 0.0f, delta_seconds);
            state.locomotion_state = LocomotionState::Climb;
            return;
        }

        // Corner mechanics (OG :1567-1630). The SIDE input is the camera-
        // relative move projected on the wall tangent (the lateral). The raw
        // XZ length is NOT the side input — a plain up-climb (stick Y) maps
        // to the camera's forward XZ, which would make move_side true and
        // fire the corner rays (climb-bug probe: the outer corner re-anchors
        // the body on a plain up-climb, teleporting it through the wall).
        // OG gates on the raw stick X (inputTranslated.X, Player.cs:1567).
        const Vec3 move_xz = context.has_move_input
            ? Vec3{context.move_input.x, 0.0f, context.move_input.z}
            : Vec3{0.0f, 0.0f, 0.0f};
        Vec3 tangent = {climb_normal.z, 0.0f, -climb_normal.x};
        const float tlen = std::sqrt((tangent.x * tangent.x) + (tangent.z * tangent.z));
        if (tlen > kEpsilon) {
            tangent.x /= tlen;
            tangent.z /= tlen;
        } else {
            tangent = {0.0f, 0.0f, 0.0f};
        }
        const float lateral = (move_xz.x * tangent.x) + (move_xz.z * tangent.z);
        const bool move_side = std::fabs(lateral) > 0.1f;

        // 1. Inner corner: sideways press into a joining wall.
        bool corner_adopted = false;
        if (room != nullptr && move_side &&
            InnerCornerSideRay(state, config_, room, climb_normal, move_xz)) {
            corner_adopted = true;
        }
        // 2. Slope-away snap: skipped when the grip probe already reports the
        // same face this tick (CONSIDER adopted — saves a raycast + avoids
        // coplanar seam-face normal flapping). Guarded on room != nullptr:
        // controller-only fixtures sustain wall-less climbs via the flag path.
        if (!corner_adopted && room != nullptr &&
            !(gripped.hit && gripped.face_id == state.climb.grip_face_id)) {
            const WallHit slope_probe = [&]() {
                WallHit out{};
                QueryWallRay(*room, ClimbWaistPoint(state, config_),
                             {-climb_normal.x, 0.0f, -climb_normal.z},
                             config_.climb_check_dist + 2.0f, &out);
                return out;
            }();
            if (slope_probe.hit &&
                std::fabs(slope_probe.plane_normal.y) < 0.35f &&
                DotXZ(slope_probe.plane_normal, climb_normal) < 0.99f) {
                AdoptCornerWall(state, config_, slope_probe);
                corner_adopted = true;
            }
        }
        // 3. Outer corner rotation: climbing around a convex edge → ease.
        if (!corner_adopted && move_side) {
            // Pre-move position for the ease origin (C1: "position before this
            // tick's corner-adopting change").
            const Vec3 pre_adopt = state.position;
            const Vec3 pre_facing = state.last_facing;
            if (OuterCornerRay(state, config_, room, climb_normal, move_xz,
                               gripped)) {
                state.climb.corner_from = pre_adopt;
                state.climb.corner_to = state.position;
                // Facing ease: from the pre-adopt facing INTO the old wall →
                // INTO the new wall (AdoptCornerWall set the new one).
                state.climb.corner_facing_from = pre_facing;
                state.climb.corner_facing_to = state.target_facing;
                state.climb.corner_ease = config_.climb_corner_ease_time;
                state.climb.corner_travel_origin = state.position;
                state.locomotion_state = LocomotionState::Climb;
                context.corner_triggered = true;
                return;
            }
        }

        // Force-corner trigger (OG :1617-1620 review SHOULD-FIX #3): measure
        // displacement against the STASHED origin — accumulates across ticks
        // (origin only resets on adopt / ease end). Per-tick displacement is
        // ≤0.67 u at climb_speed 40/60 Hz, so a per-tick > 2 test never fires;
        // OG's accumulated test does. Implemented as: travel > 2 since the
        // last origin stash → stash from/to and START the ease.
        // NOTE: the force-corner ease is only meaningful when the travel is
        // AROUND geometry (velocity got clipped by the wall plane) — a plain
        // up-climb moving > 2 units must NOT trigger it. Gate on actual
        // lateral intent AND the corner rays reporting a NEW wall this tick;
        // a pure vertical climb (climb_normal unchanged, position advancing
        // along the wall) never eases.
        bool force_corner = false;
        WallHit side_probe{};
        if (corner_adopted) {
            state.climb.corner_travel_origin = state.position;
        } else {
            const Vec3 travel = {
                state.position.x - state.climb.corner_travel_origin.x,
                state.position.y - state.climb.corner_travel_origin.y,
                state.position.z - state.climb.corner_travel_origin.z,
            };
            const float travel_len = std::sqrt(
                (travel.x * travel.x) + (travel.y * travel.y) + (travel.z * travel.z));
            if (travel_len > 2.0f && move_side && room != nullptr) {
                // OG :1617-1620: (Position - climbCornerFrom).Length() > 2 →
                // force a corner adopt: re-probe along the side ray with a
                // LONG reach; only when a genuinely new wall is found does the
                // ease start. A plain up-climb (rays miss) never eases.
                QueryWallRay(*room, ClimbWaistPoint(state, config_),
                             {-climb_normal.x, 0.0f, -climb_normal.z},
                             config_.climb_check_dist * 2.0f, &side_probe);
                if (side_probe.hit &&
                    side_probe.face_id != state.climb.grip_face_id &&
                    DotXZ(side_probe.plane_normal, climb_normal) < 0.99f) {
                    force_corner = true;
                }
            }
        }
        if (force_corner) {
            state.climb.corner_from = state.position;
            AdoptCornerWall(state, config_, side_probe);
            state.climb.corner_to = state.position;
            state.climb.corner_facing_from = state.last_facing;
            state.climb.corner_facing_to = state.target_facing;
            state.climb.corner_ease = config_.climb_corner_ease_time;
            state.climb.corner_travel_origin = state.position;
            state.locomotion_state = LocomotionState::Climb;
            context.corner_triggered = true;
            return;
        }

        // Movement along the wall plane: raw stick Y climbs up/down; camera-
        // relative horizontal intent is projected onto the wall tangent.
        const float tlen2 = std::sqrt((tangent.x * tangent.x) + (tangent.z * tangent.z));
        if (tlen2 > kEpsilon) {
            tangent.x /= tlen2;
            tangent.z /= tlen2;
        } else {
            tangent = {0.0f, 0.0f, 0.0f};
        }

        // OG climb movement (Player.cs:1476-1480): up is along the wall's UP
        // direction (UpwardPerpendicularNormal), NOT world Y. Moving straight
        // up on a sloped wall pushes the head/waist into the wall plane, and
        // the mover's popout then fires with a full-3D push (including Y) →
        // down-push + wiggle + ejection (climb-bug probe: overhang/lean-away
        // walls). wallUp = normalize((0,1,0) - n*dot((0,1,0),n)); for a
        // vertical wall this is exactly (0,1,0).
        Vec3 wall_up = {0.0f, 1.0f, 0.0f};
        {
            const float d = climb_normal.y;  // dot((0,1,0), n)
            wall_up = {climb_normal.x * -d, 1.0f - d * d, climb_normal.z * -d};
            const float ulen = std::sqrt((wall_up.x * wall_up.x) +
                                         (wall_up.y * wall_up.y) +
                                         (wall_up.z * wall_up.z));
            if (ulen > kEpsilon) {
                wall_up.x /= ulen;
                wall_up.y /= ulen;
                wall_up.z /= ulen;
            }
        }
        float up_amt = input.move.y * config_.climb_speed;
        // Cancel downward climb while grounded (OG: move.Z < 0 on ground).
        if (state.grounded && up_amt < 0.0f) {
            up_amt = 0.0f;
        }
        state.velocity.x = tangent.x * lateral * config_.climb_speed + wall_up.x * up_amt;
        state.velocity.y = wall_up.y * up_amt;
        state.velocity.z = tangent.z * lateral * config_.climb_speed + wall_up.z * up_amt;
        state.locomotion_state = LocomotionState::Climb;
}

// OG TryDash parity (Player.cs:939). The device owns the 0.10 s press
// buffer, so `input.dash_pressed` is the buffered edge; the press edge is
// consumed on acceptance. Shared by the Normal and Skidding updates — OG runs
// TryDash from both StNormalUpdate (Player.cs:882) and StSkiddingUpdate
// (Player.cs:1373), and the pre-FSM demake code had state-independent dash
// acceptance that ran before the Dashing/Normal/Skidding blocks.
bool PlayerController::TryDashAccept(
    PlayerState& state,
    const PlayerInput& input,
    StepContext& context
) const {
    if (!input.dash_pressed ||
        !state.air_dash_available ||
        state.dash_cooldown_remaining > 0.0f) {
        return false;
    }
    if (context.has_move_input) {
        state.target_facing = context.move_input;
    }
    state.target_facing = NormalizeXZ(state.target_facing, state.last_facing);
    state.last_facing = state.target_facing;
    state.dashed_on_ground = state.grounded;
    state.movement_state = PlayerMovementState::Dashing;
    state.locomotion_state = LocomotionState::Dash;
    state.air_dash_available = false;
    state.auto_jump = false;
    state.dash_hitstop_remaining = config_.dash_hitstop_time;
    state.dash_active_remaining = config_.dash_active_time;
    state.dash_time_remaining = config_.dash_hitstop_time + config_.dash_active_time;
    state.dash_reset_cooldown_remaining = config_.dash_reset_cooldown;
    state.no_dash_jump_remaining = config_.no_dash_jump_time;
    // OG CancelGroundSnap parity (Player.cs:939 TryDash): a grounded dash
    // cancels the snap so the launch cannot be pulled back down.
    state.contact.ground_snap_cooldown_remaining = config_.ground_snap_cooldown;
    context.dash_momentum = {};
    state.velocity = {};
    input.ConsumeDashPress();
    return true;
}

// OG Spring parity (Player.cs:919-937). +Y-up mapping: OG's Z (up) is our Y;
// XY is the XZ travel plane. The vector approaches are OG Calc.Approach
// directional semantics (move along the direction by at most the amount),
// which is exactly what ApproachXZ implements.
void PlayerController::Spring(PlayerState& state, const Vec3& spring_position) const {
    // Exits Climbing/Dashing — the launch always lands in Normal.
    state.movement_state = PlayerMovementState::Normal;

    // Snap her onto the board: up = spring.y + 3 (scalar, OG Position.Z
    // approach), travel plane approach = 4 (OG Position.XY()).
    state.position.y = Approach(state.position.y, spring_position.y + 3.0f, 2.0f);
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
    state.air_dash_available = true;
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

    // Climb entry — OG TryClimb parity (Player.cs:856-880 + SolidWallCheck):
    // own waist-plane query (radius climb_check_dist, prefer -facing),
    // neutral-or-into-wall input gate, ClimbNormalCheck gate (inside the
    // query), plus the OG airborne rising +4-unit snap-up variant
    // (Velocity.y > 0 && !grounded → probe waist + 4). The facing gate
    // (wall_facing_dot >= 0.5) is a KEPT documented deviation (M2): OG ranks
    // candidates by prefer only and never dot-gates; the narrower grab set
    // targets the phantom-latch complaint (movement_spec.md, Inc 5).
    {
        Vec3 entry_normal = state.wall_normal;
        const bool entry_normal_valid = LengthXZ(entry_normal) > 0.1f;
        if (!entry_normal_valid) {
            entry_normal = {-state.last_facing.x, 0.0f, -state.last_facing.z};
        }
        const float wall_facing_dot = DotXZ(entry_normal,
            {-state.last_facing.x, 0.0f, -state.last_facing.z});
        const bool facing_wall = wall_facing_dot >= 0.5f;
        const bool into_wall_input = !context.has_move_input ||
                                     DotXZ(context.move_input, entry_normal) <= -0.5f;

        // OG airborne snap-up: only grab a higher wall while RISING and
        // airborne (Velocity.y > 0, Player.cs TryClimb +4 UnitZ variant).
        const bool airborne_rising = !state.grounded && state.velocity.y > 0.0f;

        bool grab = false;
        WallHit entry_hit{};
        if (input.climb_held &&
            state.climb.cooldown_remaining <= 0.0f &&
            facing_wall &&
            into_wall_input) {
            if (room != nullptr) {
                // OG path: her OWN waist query decides — no motor flag. The
                // entry probe uses the visibility radius (wall_probe_radius,
                // = motor tangent rest distance 5 > climb_check_dist 4): at
                // exact tangency she rests with her waist one unit beyond
                // probe-4 reach, yet OG still grabs there (Popout first pulls
                // her in; dist == radius counts as touching). Entry then
                // re-anchors to the OG TryClimb spot, after which the r4
                // sustain probe governs. The +4 snap-up variant uses r4 (OG:
                // ClimbCheckDist reach above the lip).
                const Vec3 into_wall_dir = {-state.last_facing.x, 0.0f, -state.last_facing.z};
                entry_hit = QueryClimbWall(*room, ClimbWaistPoint(state, config_),
                                           config_.wall_probe_radius, into_wall_dir);
                if (!entry_hit.hit && airborne_rising) {
                    entry_hit = ClimbCheckAt(state, config_, room, 4.0f);
                }
                grab = entry_hit.hit;
            } else {
                // room == nullptr shim (controller-only fixtures): keep the
                // flag-based entry so controller-only suites keep working.
                grab = state.wall_contact;
            }
        }

        if (grab) {
            state.movement_state = PlayerMovementState::Climbing;
            state.locomotion_state = LocomotionState::Climb;
            if (room != nullptr && entry_hit.hit) {
                state.climb.wall_normal = entry_hit.plane_normal;
                state.climb.grip_face_id = entry_hit.face_id;
                // OG TryClimb reposition (Player.cs:868-869): center = wall
                // point + (half_height - waist_offset) up + pushout along the
                // normal. Single-frame snap WITHOUT the old 3-unit/axis cap —
                // the cap broke the anchor on sloped walls (the snap distance
                // is legitimately > 3 units there, e.g. 6.06 for a 15° lean),
                // leaving the body embedded in the wall plane (climb-bug
                // probe: dist=-4.93 behind the plane). The climb movement is
                // along the wall plane (wallUp) and the climb popout is
                // waist-only, so the anchor is a stable equilibrium.
                state.position = {
                    entry_hit.point.x + entry_hit.plane_normal.x * config_.climb_pushout_dist,
                    entry_hit.point.y + (config_.player_half_height - config_.climb_waist_offset),
                    entry_hit.point.z + entry_hit.plane_normal.z * config_.climb_pushout_dist,
                };
                // Tunnel guard: if the snap left her with NO wall face at the
                // climb radius, walk the snap back 50% (thin-wall safety).
                const Vec3 pre_snap = state.position;
                WallHit sanity = QueryWallNearest(*room, state.position,
                                                  config_.climb_pushout_dist);
                if (!sanity.hit) {
                    state.position = {
                        pre_snap.x + (state.position.x - pre_snap.x) * 0.5f,
                        pre_snap.y + (state.position.y - pre_snap.y) * 0.5f,
                        pre_snap.z + (state.position.z - pre_snap.z) * 0.5f,
                    };
                }
                // OG sets facing INTO the wall on grab.
                state.target_facing = NormalizeXZ(
                    {-entry_hit.plane_normal.x, 0.0f, -entry_hit.plane_normal.z},
                    state.target_facing);
                state.last_facing = state.target_facing;
            }
            state.velocity = {};
            return;
        }
    }

    // Directional wall jump (without grab): SET velocity away from the wall.
    // OG WallJumpCheck parity (Player.cs:900-910): a FRESH waist query decides
    // — not the motor's stale flag. room==nullptr shim keeps controller-only
    // fixtures on the flag path (same shim as climb entry).
    if (!state.grounded &&
        input.jump_pressed &&
        !input.jump_press_consumed) {
        bool wall_jump = false;
        Vec3 jump_dir{};
        float pushout_scale = 0.0f;
        if (room != nullptr) {
            const WallHit hit = ClimbCheckAt(state, config_, room);
            if (hit.hit) {
                wall_jump = true;
                jump_dir = NormalizeXZ(hit.plane_normal,
                    Vec3{-state.last_facing.x, 0.0f, -state.last_facing.z});
                // OG WallJumpCheck pushes her out along the face proportionally:
                // pushout * (WallPushoutDist / ClimbCheckDist).
                pushout_scale = config_.climb_pushout_dist / config_.climb_check_dist;
                const float away = hit.pushout * pushout_scale;
                state.position.x += hit.plane_normal.x * away;
                state.position.z += hit.plane_normal.z * away;
            }
        } else if (state.wall_contact) {
            wall_jump = true;
            jump_dir = NormalizeXZ(state.wall_normal,
                Vec3{-state.last_facing.x, 0.0f, -state.last_facing.z});
        }
        if (wall_jump) {
            input.ConsumeJumpPress();
            state.target_facing = jump_dir;
            state.last_facing = jump_dir;
            state.velocity.x = jump_dir.x * config_.wall_jump_speed_x;
            state.velocity.y = config_.wall_jump_speed_y;
            state.velocity.z = jump_dir.z * config_.wall_jump_speed_x;
            state.hold_jump_speed = config_.jump_speed;
            state.hold_jump_time_remaining = config_.jump_hold_time;
            // OG CancelGroundSnap parity (Player.cs:915 WallJumpCheck): the
            // wall-jump launch must not be snap-pulled back to the floor.
            state.contact.ground_snap_cooldown_remaining = config_.ground_snap_cooldown;
        }
    }

    // Dash acceptance — OG TryDash parity (Player.cs:939); shared with
    // StSkiddingUpdate. The device owns the 0.10 s press buffer, so
    // `input.dash_pressed` is the buffered edge; consumed on acceptance.
    if (TryDashAccept(state, input, context)) {
        return;  // the cascade runs StDashingUpdate in the same tick
    }

    // Normal movement block (Inc 2: moved up from after the Dashing block —
    // the dash acceptance either transitioned to Dashing (the cascade runs
    // StDashingUpdate) or left the state Normal, so the gate below runs only
    // in the Normal state, preserving the pre-FSM order).
    if (state.movement_state == PlayerMovementState::Normal) {
        // Post-mantle input lockout (OG tNoMove, Player.cs:997): while the
        // lockout is active only friction applies — move input is ignored.
        const bool move_locked = state.no_move_time_remaining > 0.0f;
        const bool move_active = has_move_input && !move_locked;
        const Vec3 horizontal_velocity = {state.velocity.x, 0.0f, state.velocity.z};
        const float speed_xz = LengthXZ(horizontal_velocity);
        // Base max speed, then apply slope modulation when grounded on a
        // non-flat normal (OG Player.cs:1017-1029): slopeDot grows with the
        // angle + along-slope input direction, scaling max UP or DOWN.
        float max_speed = config_.run_speed;
        if (state.grounded && (state.contact.ground_normal.y < 0.999f)) {
            const float slope_dot_c =
                1.0f - Clamp(state.contact.ground_normal.y, 0.0f, 1.0f);
            // Along-slope input: project move_input onto the ground normal's
            // XZ. OG uses Dot(groundNormal.XY().Normalized(), targetFacing)*2.
            const Vec3 gxz = NormalizeXZ(
                {state.contact.ground_normal.x, 0.0f, state.contact.ground_normal.z},
                move_input);
            const float along = DotXZ(gxz, move_active ? move_input : state.target_facing);
            max_speed += max_speed * (slope_dot_c * along * 2.0f);
        }
        const float desired_speed = max_speed * AnalogMagnitude(raw_input_length);
        const Vec3 target_velocity = move_active
            ? Vec3{move_input.x * desired_speed, state.velocity.y, move_input.z * desired_speed}
            : Vec3{0.0f, state.velocity.y, 0.0f};

        if (state.grounded) {
            // OG: rotation-based turning when above rotate threshold.
            if (move_active && speed_xz > config_.rotate_threshold) {
                // Skid trigger (og-mover-overhaul Inc 4, OG Player.cs:1072-1078):
                // when moving fast AND the camera-relative input opposes the
                // velocity direction past SkidDotThreshold, enter Skidding.
                const Vec3 vel_norm = NormalizeXZ(horizontal_velocity, move_input);
                const float turn_dot = DotXZ(vel_norm, move_input);
                if (turn_dot <= config_.skid_dot_threshold) {
                    state.movement_state = PlayerMovementState::Skidding;
                    state.no_skid_jump_remaining = config_.no_skid_jump_time;
                    state.target_facing = move_input;
                    state.facing = move_input;
                    state.locomotion_state = LocomotionState::Idle;
                    return;
                }
                state.target_facing = RotateTowardXZ(state.target_facing, move_input, config_.rotate_speed * delta_seconds);
                const float accel = config_.acceleration;
                const Vec3 rotated_target = {
                    state.target_facing.x * desired_speed,
                    state.velocity.y,
                    state.target_facing.z * desired_speed,
                };
                state.velocity = ApproachXZ(state.velocity, rotated_target, accel * delta_seconds);
            } else {
                const float accel = move_active
                    ? config_.acceleration
                    : config_.friction;
                state.velocity = ApproachXZ(state.velocity, target_velocity, accel * delta_seconds);
            }
            state.locomotion_state = move_active || speed_xz > kEpsilon
                ? LocomotionState::Run
                : LocomotionState::Idle;
            if (move_active) {
                state.target_facing = move_input;
                state.facing = move_input;
            }
        } else {
            // Air movement: OG scales accel by dot product of input vs velocity direction
            const float input_dot = move_active
                ? DotXZ(NormalizeXZ(horizontal_velocity, state.target_facing), move_input)
                : 1.0f;
            float accel_mult = 1.0f;
            if (input_dot < 0.0f) {
                // Reversing direction: more accel (air_turn)
                accel_mult = config_.air_accel_mult_min + (config_.air_accel_mult_max - config_.air_accel_mult_min) * (1.0f + input_dot);
            }
            float accel;
            if (speed_xz > config_.run_speed) {
                // Above max: past_max_decel (slower acceleration to preserve momentum)
                accel = config_.past_max_deceleration * accel_mult;
            } else {
                accel = config_.acceleration * accel_mult;
            }
            if (move_active) {
                // OG: rotation-based turning in air too
                if (speed_xz > config_.rotate_threshold) {
                    state.target_facing = RotateTowardXZ(state.target_facing, move_input, config_.rotate_speed * delta_seconds);
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

        ApplyJumpAndGravity(state, input, move_input, config_, delta_seconds);
    }
}

void PlayerController::StDashingUpdate(
    PlayerState& state,
    const PlayerInput& input,
    StepContext& context,
    float delta_seconds,
    const Room* room
) const {
    const Vec3& move_input = context.move_input;
    const bool has_move_input = context.has_move_input;
    (void)room;
    if (state.movement_state == PlayerMovementState::Dashing) {
            state.dash_time_remaining -= delta_seconds;
            if (state.dash_hitstop_remaining > 0.0f) {
                state.dash_hitstop_remaining = Approach(state.dash_hitstop_remaining, 0.0f, delta_seconds);
                state.velocity = {};
                return;
            }
            state.dash_active_remaining = Approach(state.dash_active_remaining, 0.0f, delta_seconds);
            // Dash steering (og-mover-overhaul Inc 4, OG Player.cs:1288-1293):
            // while dashing, rotate toward move input and re-set dash speed so
            // the player can arc the dash. OG: steer while Dot(move, facing) >= -0.2.
            const Vec3 dash_dir = NormalizeXZ(state.target_facing, state.last_facing);
            if (has_move_input && DotXZ(move_input, dash_dir) >= -0.2f) {
                state.target_facing = RotateTowardXZ(
                    dash_dir, move_input, config_.dash_rotate_speed * delta_seconds);
            }
            state.velocity.x = state.target_facing.x * config_.dash_speed;
            state.velocity.y = 0.0f;
            state.velocity.z = state.target_facing.z * config_.dash_speed;
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
                    input.jump_pressed) {
                    input.ConsumeJumpPress();
                    state.movement_state = PlayerMovementState::Normal;
                    StartDashJump(state, config_, move_input, context);
                }
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
    // OG StSkiddingUpdate runs TryDash() FIRST (Player.cs:1373): a dash press
    // while skidding fires a dash. The pre-FSM code had state-independent dash
    // acceptance, so a skid dash always worked; the FSM restores it here.
    if (TryDashAccept(state, input, context)) {
        return;  // the cascade runs StDashingUpdate in the same tick
    }
    // Skidding state (og-mover-overhaul Inc 4, OG StSkiddingUpdate
    // Player.cs:1371-1429). Skid decel toward the input direction; exit on
    // (a) no/low input, (b) skid-done (aligned + up to EndSkidSpeed),
    // (c) leaving the ground, (d) skid-jump into Normal.
    if (state.movement_state == PlayerMovementState::Skidding) {
            const Vec3 skid_vel = {state.velocity.x, 0.0f, state.velocity.z};
            const Vec3 skid_norm = NormalizeXZ(skid_vel, move_input);
            // cancel conditions (OG: input < .2, dot < .7 with facing, or !onGround)
            const float mag2 = (input.move.x * input.move.x) + (input.move.y * input.move.y);
            const float input_to_facing = has_move_input ? DotXZ(move_input, state.target_facing) : 0.0f;
            if (mag2 < 0.2f * 0.2f || (has_move_input && input_to_facing < 0.7f) || !state.grounded) {
                state.movement_state = PlayerMovementState::Normal;
            } else {
                // skid-jump
                if (state.no_skid_jump_remaining <= 0.0f && input.ConsumeJumpPress()) {
                    state.movement_state = PlayerMovementState::Normal;
                    // OG CancelGroundSnap parity: the skid-jump launch must not
                    // be snap-pulled back to the floor.
                    state.contact.ground_snap_cooldown_remaining = config_.ground_snap_cooldown;
                    state.velocity.x = move_input.x * config_.skid_jump_xy_speed;
                    state.velocity.z = move_input.z * config_.skid_jump_xy_speed;
                    state.velocity.y = config_.skid_jump_speed;
                    state.hold_jump_speed = config_.skid_jump_speed;
                    state.hold_jump_time_remaining = config_.skid_jump_hold_time;
                    state.locomotion_state = LocomotionState::Jump;
                } else {
                    // decel toward input direction
                    const bool dot_matches = DotXZ(skid_norm, move_input) >= 0.7f;
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
                        state.movement_state = PlayerMovementState::Normal;
                    }
                }
            }
        }
}

void PlayerController::OnStateEnter(
    PlayerState& state,
    PlayerMovementState prev,
    PlayerMovementState next
) const {
    (void)state; (void)prev; (void)next;
    // Inc 2: enter hook. Currently a no-op — future states (Feather, etc.)
    // will use it for enter-time setup. The Land event parity (OG Events.Land)
    // is also a no-op hook for now (the grounded edge is handled inline).
}

void PlayerController::OnStateExit(
    PlayerState& state,
    PlayerMovementState prev,
    PlayerMovementState next
) const {
    (void)state; (void)prev; (void)next;
    // Inc 2: exit hook. Currently a no-op.
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

    // Inc 2: real state machine. StatePhase is now a dispatcher over the four
    // locomotion states (Normal/Dashing/Skidding/Climbing); each St*Update is
    // the pre-FSM hand-switched body moved verbatim (no logic reordering).
    //
    // Climb ticks are terminal: the pre-FSM climb block always returned,
    // skipping the facing rotation for the whole tick (incl. release/mantle/
    // walljump/dash transitions).
    if (state.movement_state == PlayerMovementState::Climbing) {
        StClimbingUpdate(state, input, context, delta_seconds, room);
        return;
    }

    // Transition cascade: the pre-FSM code ran the Dashing/Skidding blocks in
    // the SAME tick as the dash/skid acceptance (Normal→Dashing→StDashingUpdate,
    // Normal→Skidding→StSkiddingUpdate) and the Normal block in the same tick
    // as the dash end (Dashing→Normal→StNormalUpdate). The loop reproduces
    // that; enter/exit hooks fire on each transition. Climb entry and skid
    // exits are terminal for the tick (the pre-FSM code returned after them).
    for (int i = 0; i < 3; ++i) {
        const PlayerMovementState before = state.movement_state;
        switch (state.movement_state) {
            case PlayerMovementState::Normal:
                StNormalUpdate(state, input, context, delta_seconds, room);
                break;
            case PlayerMovementState::Dashing:
                StDashingUpdate(state, input, context, delta_seconds, room);
                break;
            case PlayerMovementState::Skidding:
                StSkiddingUpdate(state, input, context, delta_seconds, room);
                break;
            case PlayerMovementState::Climbing:
                // Unreachable in the loop (Climbing is handled above).
                StClimbingUpdate(state, input, context, delta_seconds, room);
                break;
        }
        if (state.movement_state == before) {
            break;  // no transition this tick
        }
        OnStateExit(state, before, state.movement_state);
        OnStateEnter(state, before, state.movement_state);
        if (state.movement_state == PlayerMovementState::Climbing ||
            (before == PlayerMovementState::Skidding &&
             state.movement_state != PlayerMovementState::Dashing)) {
            break;  // climb entry / skid exit: terminal for the tick
            // (Skidding→Dashing still cascades: the pre-FSM dash acceptance
            // ran before the state blocks, so a skid dash ran the Dashing
            // block in the same tick; Skidding→Normal cancel/done/jump only
            // fell through to the facing rotation.)
        }
    }

    // Facing rotation (runs for all states except Dashing; climb ticks return
    // above, so Climbing never reaches here).
    if (state.movement_state != PlayerMovementState::Dashing) {
        state.facing = RotateTowardXZ(state.facing, state.target_facing, 12.566371f * delta_seconds);
    }
}

void PlayerController::MovePhase(
    PlayerState& state,
    float delta_seconds,
    const Room* room
) const {
    if (delta_seconds <= 0.0f) {
        return;
    }
    if (room == nullptr) {
        LateContactPhase(state, delta_seconds, nullptr);
        return;
    }

    // OG Single-update move + popout (Player.cs:341-373, +Y): after the state
    // handler edits velocity, SweepMove applies velocity*dt through the ONE
    // mover, then Popout(false) for good measure. Ground snap then pulls the
    // feet down within 5 units when recently grounded and the cooldown has
    // elapsed (OG LateUpdate).
    const bool tNoMove = state.no_move_time_remaining > 0.0f;
    const Vec3 amount = {
        state.velocity.x * delta_seconds,
        state.velocity.y * delta_seconds,
        state.velocity.z * delta_seconds,
    };
    if (state.movement_state == PlayerMovementState::Climbing) {
        // Climb-bug fix: the climb moves along the wall plane with a
        // waist-only popout (SweepMoveClimb). The full SweepMove's head wall
        // query + ceiling check fight the climb on sloped walls (down-push,
        // wiggle, ejection).
        mover_.SweepMoveClimb(state, *room, amount);
    } else {
        mover_.SweepMove(state, *room, amount, !tNoMove);
    }

    // OG LateUpdate ground checks (Player.cs:400-411): recompute grounded from
    // the post-move position, then ground snap if we left the ground.
    const bool prev_grounded = state.contact.was_grounded;
    state.grounded = mover_.GroundCheck(state, *room, nullptr, nullptr);
    if (!state.grounded && prev_grounded) {
        mover_.GroundSnap(state, *room, delta_seconds);
    }

    // Coyote refresh + dash refill when grounded (OG: tCoyote reset, dash
    // refill on ground within reset window).
    if (state.grounded) {
        state.coyote_time_remaining = config_.coyote_time;
        state.contact.coyote_height = state.position.y;
        if (state.dash_reset_cooldown_remaining <= 0.0f) {
            state.air_dash_available = true;
        }
    }

    LateContactPhase(state, delta_seconds, room);
}

void PlayerController::LateContactPhase(
    PlayerState& state,
    float delta_seconds,
    const Room* room
) const {
    (void)delta_seconds;
    (void)room;
    state.contact.was_grounded = state.grounded;
}

}  // namespace madeline_cube
