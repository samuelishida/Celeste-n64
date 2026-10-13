// Host replay of the guided climb route against the checked-in real-map CMSH.
// Unlike the device autoplay, this can run while a user-owned Ares session is
// open. It uses manifest Start, ordinary controller movement for approach,
// real TryClimb entry, sustained climb, and mantle; no position teleport.

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "gameplay/physics/coll_mesh.hpp"
#include "gameplay/player/player_controller.hpp"
#include "gameplay/player/player_model_clearance.hpp"
#include "gameplay/world/mappack_loader.hpp"
#include "gameplay/world/world.hpp"

using namespace madeline_cube;
using namespace madeline_cube::physics;

namespace {

constexpr float kDt = 1.0f / 60.0f;
constexpr float kTau = 6.28318530717958647692f;
constexpr int kDirectionSamples = 16;
constexpr float kHeightOffsets[] = {0.0f, 4.0f, -4.0f, 8.0f, -8.0f};
constexpr float kPenetrationTolerance = 0.10f;

float DotXZ(const Vec3& a, const Vec3& b) {
    return a.x * b.x + a.z * b.z;
}

float DistanceXZ(const Vec3& a, const Vec3& b) {
    const float dx = a.x - b.x;
    const float dz = a.z - b.z;
    return std::sqrt(dx * dx + dz * dz);
}

float Distance3(const Vec3& a, const Vec3& b) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    const float dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

struct ClimbTarget {
    WallHit wall{};
    Vec3 approach{};
    float start_gap = 0.0f;
    float search_distance = 0.0f;
    float supported_height = 0.0f;
    int height_band = -1;
};

bool FindGuidedTarget(const Room& room, const Vec3& waist,
                      float check_distance, ClimbTarget& out) {
    for (float distance = 2.0f; distance <= 120.0f; distance += 4.0f) {
        for (int band = 0; band < 5; ++band) {
            for (int sample = 0; sample < kDirectionSamples; ++sample) {
                const float angle = kTau * static_cast<float>(sample) /
                                    static_cast<float>(kDirectionSamples);
                const Vec3 direction = {std::cos(angle), 0.0f, std::sin(angle)};
                const Vec3 probe = {
                    waist.x + direction.x * distance,
                    waist.y + kHeightOffsets[band],
                    waist.z + direction.z * distance,
                };
                const WallHit candidate = QueryClimbWall(
                    room, probe, check_distance,
                    {-direction.x, 0.0f, -direction.z});
                if (!candidate.hit || DotXZ(candidate.plane_normal, direction) >= -0.5f)
                    continue;

                float supported_height = 0.0f;
                for (float height = 3.0f; height <= 48.0f; height += 3.0f) {
                    const WallHit above = QueryClimbWall(
                        room,
                        {candidate.point.x, candidate.point.y + height,
                         candidate.point.z},
                        check_distance,
                        {-direction.x, 0.0f, -direction.z});
                    const float alignment = DotXZ(above.plane_normal,
                                                  candidate.plane_normal);
                    if (!above.hit || alignment < 0.8f) break;
                    supported_height = height;
                }
                if (supported_height < 9.0f) continue;

                out.wall = candidate;
                out.approach = {-candidate.plane_normal.x, 0.0f,
                                -candidate.plane_normal.z};
                const float len = std::sqrt(DotXZ(out.approach, out.approach));
                if (len <= 1e-5f) return false;
                out.approach.x /= len;
                out.approach.z /= len;
                out.start_gap = Dot(candidate.plane_normal,
                                    {waist.x - candidate.point.x,
                                     waist.y - candidate.point.y,
                                     waist.z - candidate.point.z});
                out.search_distance = distance;
                out.supported_height = supported_height;
                out.height_band = band;
                return true;
            }
        }
    }
    return false;
}

struct ClearanceStats {
    int samples = 0;
    int violations = 0;
    float max_pushout = 0.0f;
    int worst_face = -1;
    const char* worst_probe = "none";
    int model_samples = 0;
    int model_violations = 0;
    float min_model_gap = INFINITY;
    float max_model_offset = 0.0f;
};

bool SampleWallClearance(const Room& room, const PlayerState& state,
                         const MovementConfig& config,
                         ClearanceStats& stats, int tick) {
    const Vec3 feet = {state.position.x,
                       state.position.y - config.player_half_height,
                       state.position.z};
    const Vec3 probes[] = {
        {feet.x, feet.y + config.climb_waist_offset, feet.z},
        {feet.x, feet.y + config.climb_head_offset, feet.z},
    };
    const char* names[] = {"waist", "head"};
    for (int i = 0; i < 2; ++i) {
        const WallHit hit = QueryWallPushout(room, probes[i],
                                             config.player_wall_radius);
        ++stats.samples;
        if (!hit.hit) continue;
        const bool is_worst = hit.pushout > stats.max_pushout;
        stats.max_pushout = std::max(stats.max_pushout, hit.pushout);
        if (is_worst || stats.worst_face < 0) {
            stats.worst_face = hit.face_id;
            stats.worst_probe = names[i];
        }
        if (hit.pushout > kPenetrationTolerance) {
            ++stats.violations;
            std::printf("FAIL: tick=%d %s overlap=%.3f face=%d pos=(%.2f,%.2f,%.2f)\n",
                        tick, names[i], hit.pushout, hit.face_id,
                        state.position.x, state.position.y, state.position.z);
            return false;
        }
    }
    return true;
}

bool RecordModelGap(ClearanceStats& stats, float gap, float offset,
                    const PlayerState& state, int face, int tick) {
    ++stats.model_samples;
    stats.min_model_gap = std::min(stats.min_model_gap, gap);
    stats.max_model_offset = std::max(stats.max_model_offset, offset);
    if (gap >= kMadelineModelWallGap - 0.01f) return true;
    ++stats.model_violations;
    std::printf("FAIL: tick=%d rendered-model gap=%.3f required=%.3f face=%d "
                "offset=%.3f pos=(%.2f,%.2f,%.2f)\n",
                tick, gap, kMadelineModelWallGap, face, offset,
                state.position.x, state.position.y, state.position.z);
    return false;
}

bool SampleRenderedModelClearance(const Room& room, const PlayerState& state,
                                  const MovementConfig& config,
                                  ClearanceStats& stats, int tick) {
    const Vec3 feet = {state.position.x,
                       state.position.y - config.player_half_height,
                       state.position.z};
    if (state.movement_state == PlayerMovementState::Climbing &&
        state.climb.corner_ease > 0.0f) {
        const Vec3 normal_a = state.climb.corner_normal_from;
        const Vec3 normal_b = state.climb.corner_normal_to;
        const float anchor_y = config.player_half_height - config.climb_waist_offset;
        const Vec3 plane_a = {
            state.climb.corner_from.x - normal_a.x * config.climb_pushout_dist,
            state.climb.corner_from.y - normal_a.y * config.climb_pushout_dist - anchor_y,
            state.climb.corner_from.z - normal_a.z * config.climb_pushout_dist,
        };
        const Vec3 plane_b = {
            state.climb.corner_to.x - normal_b.x * config.climb_pushout_dist,
            state.climb.corner_to.y - normal_b.y * config.climb_pushout_dist - anchor_y,
            state.climb.corner_to.z - normal_b.z * config.climb_pushout_dist,
        };
        const float gap_a = Dot(normal_a,
            {feet.x - plane_a.x, feet.y - plane_a.y, feet.z - plane_a.z});
        const float gap_b = Dot(normal_b,
            {feet.x - plane_b.x, feet.y - plane_b.y, feet.z - plane_b.z});
        const Vec3 offset = PlayerModelCornerHorizontalOffset(
            normal_a, gap_a, normal_b, gap_b, kMadelineModelRadius,
            kMadelineModelHeight, kMadelineModelWallGap);
        const float offset_length = std::sqrt(DotXZ(offset, offset));
        const float model_gap_a = PlayerModelMinimumPlaneGapAfterTranslation(
            normal_a, gap_a, kMadelineModelRadius, kMadelineModelHeight, offset);
        const float model_gap_b = PlayerModelMinimumPlaneGapAfterTranslation(
            normal_b, gap_b, kMadelineModelRadius, kMadelineModelHeight, offset);
        return RecordModelGap(stats, model_gap_a, offset_length, state,
                              state.climb.grip_face_id, tick) &&
               RecordModelGap(stats, model_gap_b, offset_length, state,
                              state.climb.grip_face_id, tick);
    }

    const bool climbing = state.movement_state == PlayerMovementState::Climbing;
    if (climbing) {
        PlayerModelPlaneConstraint constraints[kMaxPlayerModelWallConstraints];
        int count = QueryPlayerModelWallConstraints(
            room, feet, kMadelineModelRadius, kMadelineModelHeight,
            config.climb_waist_offset, kMadelineModelWallGap, constraints,
            kMaxPlayerModelWallConstraints);
        if (count == 0) {
            const Vec3 normal = state.climb.wall_normal;
            constraints[0] = {
                normal,
                config.climb_pushout_dist -
                    config.climb_waist_offset * normal.y,
            };
            count = 1;
        }
        const Vec3 offset = PlayerModelMultiPlaneHorizontalOffset(
            constraints, count, kMadelineModelRadius, kMadelineModelHeight,
            kMadelineModelWallGap);
        const float offset_length = std::sqrt(DotXZ(offset, offset));
        for (int i = 0; i < count; ++i) {
            const float model_gap = PlayerModelMinimumPlaneGapAfterTranslation(
                constraints[i].normal, constraints[i].feet_signed_distance,
                kMadelineModelRadius, kMadelineModelHeight, offset);
            if (!RecordModelGap(stats, model_gap, offset_length, state, -1, tick))
                return false;
        }
        return true;
    }

    const Vec3 waist = {feet.x, feet.y + config.climb_waist_offset, feet.z};
    const Vec3 prefer_dir = {
        -state.last_facing.x, 0.0f, -state.last_facing.z};
    const WallHit wall = QueryClimbWall(
        room, waist, kMadelineModelRadius + kMadelineModelWallGap, prefer_dir);
    if (!wall.hit) return true;
    const float feet_gap = Dot(wall.plane_normal,
        {feet.x - wall.point.x, feet.y - wall.point.y, feet.z - wall.point.z});
    const float offset = PlayerModelHorizontalOffset(
        wall.plane_normal, feet_gap, kMadelineModelRadius,
        kMadelineModelHeight, kMadelineModelWallGap);
    const float model_gap = PlayerModelMinimumPlaneGap(
        wall.plane_normal, feet_gap, kMadelineModelRadius,
        kMadelineModelHeight, offset);
    return RecordModelGap(stats, model_gap, offset, state, wall.face_id, tick);
}

bool IsFinitePosition(const Vec3& p) {
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
}

}  // namespace

int main(int argc, char** argv) {
    MapSpecV2 map{};
    const char* pack_path = argc > 1 ? argv[1]
        : "build/bake-fc-1200/staging/forsyken-city.mappack";
    const char* mesh_path = argc > 2 ? argv[2]
        : "build/bake-fc-1200/staging/forsyken-city.colmesh";
    if (!LoadMapPackV2(pack_path, map)) {
        std::printf("FAIL: cannot load %s\n", pack_path);
        return 1;
    }
    const V2SpawnSpec* start = map.FindStartSpawn();
    if (!start) {
        std::printf("FAIL: map-pack has no Start spawn\n");
        return 1;
    }
    CollMesh* mesh = LoadCollMesh(mesh_path);
    if (!mesh) {
        std::printf("FAIL: cannot load %s\n", mesh_path);
        return 1;
    }

    Room room;
    room.coll_mesh = mesh;
    MovementConfig config;
    PlayerController controller(config);
    PlayerState state;
    state.position = start->position;
    // Match GameplayScene::ResetPlayerToRoomStart: map spawns are feet points;
    // lift to center, snap through GroundCheck, and retain spawn skin.
    constexpr float kSpawnSkin = 0.2f;
    state.position.y += config.player_half_height + kSpawnSkin;
    Vec3 spawn_pushout{};
    Vec3 ground_normal{};
    if (controller.Mover().GroundCheck(state, room, &spawn_pushout,
                                      &ground_normal)) {
        state.position.y += spawn_pushout.y + kSpawnSkin;
    }
    state.prev_position = state.position;
    state.grounded = controller.Mover().GroundCheck(
        state, room, nullptr, &ground_normal);
    state.contact.was_grounded = state.grounded;
    if (state.grounded) {
        state.contact.ground_normal = ground_normal;
        state.coyote_time_remaining = config.coyote_time;
    }

    const Vec3 start_position = state.position;
    const Vec3 waist = {state.position.x,
                        state.position.y - config.player_half_height +
                            config.climb_waist_offset,
                        state.position.z};
    ClimbTarget target;
    if (!FindGuidedTarget(room, waist, config.climb_check_dist, target)) {
        std::printf("FAIL: no supported real-map wall in 120-unit search\n");
        FreeCollMesh(mesh);
        return 1;
    }
    state.facing = target.approach;
    state.target_facing = target.approach;
    state.last_facing = target.approach;

    std::printf("[real-map-physics] start=(%.2f,%.2f,%.2f) face=%d search=%.1f "
                "support=%.1f start_gap=%.2f approach=(%.2f,%.2f)\n",
                start_position.x, start_position.y, start_position.z,
                target.wall.face_id, target.search_distance,
                target.supported_height, target.start_gap,
                target.approach.x, target.approach.z);

    ClearanceStats clearance;
    int approach_ticks = 0;
    int climb_ticks = 0;
    float sustained_climb_rise = 0.0f;
    int wall_rest_ticks = 0;
    float entry_gap = 1.0e9f;
    float entry_displacement = 0.0f;
    float max_wall_rest_drift = 0.0f;
    float max_tick_displacement = 0.0f;
    Vec3 wall_rest_anchor{};
    bool climb_started = false;
    bool mantled = false;
    bool wall_contact_seen = false;
    bool movement_ok = true;
    constexpr int kMaxApproachTicks = 120;
    constexpr int kMaxClimbTicks = 80;
    constexpr int kRequiredWallRestTicks = 30;

    for (int tick = 0; tick < kMaxApproachTicks + kMaxClimbTicks; ++tick) {
        const bool was_climbing =
            state.movement_state == PlayerMovementState::Climbing;
        const Vec3 tick_start_position = state.position;
        state.prev_position = state.position;
        PlayerInput input;
        input.move = {0.0f, 1.0f};
        if (!was_climbing) {
            const Vec3 current_waist = {
                state.position.x,
                state.position.y - config.player_half_height +
                    config.climb_waist_offset,
                state.position.z,
            };
            const float gap = Dot(target.wall.plane_normal,
                {current_waist.x - target.wall.point.x,
                 current_waist.y - target.wall.point.y,
                 current_waist.z - target.wall.point.z});
            const bool target_wall_contact = state.wall_contact &&
                gap <= config.player_wall_radius + 0.5f;
            if (target_wall_contact) {
                wall_contact_seen = true;
                if (std::sqrt(DotXZ(state.velocity, state.velocity)) < 0.01f) {
                    if (wall_rest_ticks == 0) wall_rest_anchor = state.position;
                    ++wall_rest_ticks;
                    max_wall_rest_drift = std::max(
                        max_wall_rest_drift,
                        DistanceXZ(wall_rest_anchor, state.position));
                } else {
                    // Let residual tangent velocity settle before starting
                    // the stationary-contact measurement window.
                    wall_rest_ticks = 0;
                    max_wall_rest_drift = 0.0f;
                }
                // Separate collision rest from OG ledge-avoid steering. The
                // selected wall borders the starting ledge, so continued
                // forward input intentionally rotates toward supported floor.
                input.move = {};
            } else {
                wall_rest_ticks = 0;
                max_wall_rest_drift = 0.0f;
                // Once collision confirms target wall, do not resume forward
                // input during contact-flag flicker while measuring rest.
                if (wall_contact_seen) input.move = {};
            }
            if (wall_rest_ticks >= kRequiredWallRestTicks &&
                gap <= config.climb_check_dist) {
                input.move = {0.0f, 1.0f};
                input.climb_held = true;
                if (entry_gap > 1.0e8f) {
                    entry_gap = gap;
                    entry_displacement = DistanceXZ(start_position,
                                                    state.position);
                }
            }
        } else {
            input.climb_held = true;
            ++climb_ticks;
        }

        const Vec3 camera_forward = target.approach;
        controller.BeginMoveTick(state);
        PlayerController::StepContext context = controller.TimerInputPhase(
            state, input, camera_forward, kDt);
        const float before_climb_y = state.position.y;
        controller.StatePhase(state, input, context, kDt, &room);
        controller.MovePhase(state, kDt, &room, &context);
        if (was_climbing && state.movement_state == PlayerMovementState::Climbing)
            sustained_climb_rise += std::fmax(0.0f, state.position.y - before_climb_y);
        if (state.movement_state == PlayerMovementState::Climbing)
            climb_started = true;
        if (was_climbing && state.movement_state != PlayerMovementState::Climbing &&
            !context.mantle_triggered) {
            std::printf("FAIL: climb exited without mantle at tick=%d pos=(%.2f,%.2f,%.2f)\n",
                        tick, state.position.x, state.position.y,
                        state.position.z);
            movement_ok = false;
            break;
        }
        if (context.mantle_triggered) mantled = true;
        if (!was_climbing && wall_rest_ticks > 0 &&
            (wall_rest_ticks == 1 || wall_rest_ticks == 15 || wall_rest_ticks == 30)) {
            std::printf("[real-map-physics] rest_tick=%d pos=(%.3f,%.3f,%.3f) "
                        "v=(%.3f,%.3f,%.3f) drift=%.3f wall=%u ground=%u\n",
                        wall_rest_ticks, state.position.x, state.position.y,
                        state.position.z, state.velocity.x, state.velocity.y,
                        state.velocity.z,
                        DistanceXZ(wall_rest_anchor, state.position),
                        state.wall_contact ? 1u : 0u, state.grounded ? 1u : 0u);
        }
        max_tick_displacement = std::max(
            max_tick_displacement,
            Distance3(tick_start_position, state.position));
        if (was_climbing && (climb_ticks <= 3 || climb_ticks % 10 == 0)) {
            std::printf("[real-map-physics] climb_tick=%d y=%.2f vy=%.2f state=%d "
                        "wall=(%.2f,%.2f,%.2f)\n",
                        climb_ticks, state.position.y, state.velocity.y,
                        static_cast<int>(state.movement_state),
                        state.climb.wall_normal.x, state.climb.wall_normal.y,
                        state.climb.wall_normal.z);
        }

        if (!IsFinitePosition(state.position) || !IsFinitePosition(state.velocity)) {
            std::printf("FAIL: non-finite state tick=%d\n", tick);
            movement_ok = false;
            break;
        }
        if (!SampleWallClearance(room, state, config, clearance, tick)) {
            movement_ok = false;
            break;
        }
        constexpr float kRenderAlphas[] = {0.0f, 0.25f, 0.5f, 0.75f, 1.0f};
        for (float alpha : kRenderAlphas) {
            PlayerState render_state = state;
            render_state.position = state.InterpolatedPosition(alpha);
            if (!SampleRenderedModelClearance(
                    room, render_state, config, clearance, tick)) {
                movement_ok = false;
                break;
            }
        }
        if (!movement_ok) break;
        if (!was_climbing && !climb_started && !state.wall_contact)
            ++approach_ticks;
        if (mantled) break;
        if (climb_started && climb_ticks >= kMaxClimbTicks) {
            std::printf("FAIL: climb did not mantle within %d ticks\n",
                        kMaxClimbTicks);
            movement_ok = false;
            break;
        }
    }

    const bool approach_ok = entry_gap <= config.climb_check_dist + 0.01f &&
        target.start_gap - entry_gap >= 4.0f &&
        entry_displacement >= 4.0f;
    const bool wall_rest_ok = wall_rest_ticks >= kRequiredWallRestTicks &&
        max_wall_rest_drift <= 0.25f;
    const bool motion_bounds_ok = max_tick_displacement <= 5.1f;
    // This short real wall can mantle one tick sooner when the lip query
    // correctly observes the resolved movement. Prove sustained climbing by
    // measured rise and multiple ticks, rather than an arbitrary duration.
    const bool climb_ok = climb_started && climb_ticks >= 2 &&
                          sustained_climb_rise >= 4.0f && mantled;
    const bool clearance_ok = clearance.samples >= 20 &&
        clearance.violations == 0 &&
        clearance.max_pushout <= kPenetrationTolerance;
    const bool model_clearance_ok = clearance.model_samples >= 10 &&
        clearance.model_violations == 0 &&
        clearance.min_model_gap >= kMadelineModelWallGap - 0.01f;
    std::printf("[real-map-physics] result=%s approach=%u approach_ticks=%d "
                "wall_rest=%u rest_ticks=%d rest_drift=%.3f max_step=%.3f "
                "entry_gap=%.2f displacement=%.2f climb=%u climb_ticks=%d climb_rise=%.2f "
                "mantle=%u clearance_samples=%d overlap_violations=%d "
                "max_pushout=%.3f worst=%s face=%d model_samples=%d "
                "model_violations=%d min_model_gap=%.3f max_model_offset=%.3f "
                "end=(%.2f,%.2f,%.2f)\n",
                movement_ok && approach_ok && wall_rest_ok && motion_bounds_ok &&
                        climb_ok && clearance_ok && model_clearance_ok
                    ? "PASS" : "FAIL",
                approach_ok ? 1u : 0u, approach_ticks,
                wall_rest_ok ? 1u : 0u, wall_rest_ticks,
                max_wall_rest_drift, max_tick_displacement, entry_gap,
                entry_displacement, climb_ok ? 1u : 0u, climb_ticks, sustained_climb_rise,
                mantled ? 1u : 0u, clearance.samples,
                clearance.violations, clearance.max_pushout,
                clearance.worst_probe, clearance.worst_face,
                clearance.model_samples, clearance.model_violations,
                clearance.min_model_gap, clearance.max_model_offset,
                state.position.x, state.position.y, state.position.z);

    FreeCollMesh(mesh);
    return movement_ok && approach_ok && wall_rest_ok && motion_bounds_ok &&
        climb_ok && clearance_ok && model_clearance_ok ? 0 : 1;
}
