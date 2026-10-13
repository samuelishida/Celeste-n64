// Exercise a real, vertical Forsaken City corner through normal climb entry,
// side intent, corner adoption, and ease. Synthetic corners are too small to
// expose a long outer-ray anchor jump on this map.
#include <cmath>
#include <cstdio>
#include <limits>

#include "gameplay/physics/coll_mesh.hpp"
#include "gameplay/player/player_controller.hpp"
#include "gameplay/player/player_model_clearance.hpp"
#include "gameplay/world/mappack_loader.hpp"
#include "gameplay/world/world.hpp"

using namespace madeline_cube;
using namespace madeline_cube::physics;

namespace {

constexpr float kDt = 1.0f / 60.0f;
constexpr float kMaxCornerStep = 6.0f;
constexpr float kMaxWaistPushout = 0.50f;
// Two nonopposing vertical planes may require a diagonal render translation.
constexpr float kMaxRenderOffset = 1.41421356237f *
    (kMadelineModelRadius + kMadelineModelWallGap + kMaxWaistPushout);

struct RouteMetrics {
    float max_step = 0.0f;
    float max_waist_pushout = 0.0f;
    float max_head_probe_pushout = 0.0f;
    float min_model_gap = std::numeric_limits<float>::infinity();
    float max_render_offset = 0.0f;
    float min_corner_feet_gap = INFINITY;
    float min_corner_normal_dot = INFINITY;
    int model_samples = 0;
    int corner_events = 0;
    int exits = 0;
    bool adopted_target_wall = false;
};

}  // namespace

int main(int argc, char** argv) {
    const char* pack_path = argc > 1 ? argv[1]
        : "build/bake-fc-1200/staging/forsyken-city.mappack";
    const char* mesh_path = argc > 2 ? argv[2]
        : "build/bake-fc-1200/staging/forsyken-city.colmesh";
    MapSpecV2 map{};
    if (!LoadMapPackV2(pack_path, map)) {
        std::printf("FAIL: cannot load %s\n", pack_path);
        return 1;
    }
    const V2SpawnSpec* start = map.FindStartSpawn();
    CollMesh* mesh = LoadCollMesh(mesh_path);
    if (!start || !mesh) {
        std::printf("FAIL: missing map Start or cannot load %s\n", mesh_path);
        return 1;
    }

    Room room{};
    room.coll_mesh = mesh;
    MovementConfig config{};
    PlayerController controller(config);
    // Real shared vertical edge near Start. The approach face is z=38.4;
    // travel from manifest Start before sustained side intent rounds its finite
    // edge onto the perpendicular wall. No player-position teleport is used.
    constexpr Vec3 edge{0.0f, 32.0f, 38.4f};
    constexpr Vec3 old_normal{0.0f, 0.0f, 1.0f};
    PlayerState state{};
    state.position = start->position;
    state.position.y += config.player_half_height + 0.2f;
    Vec3 spawn_pushout{};
    Vec3 spawn_normal{};
    if (controller.Mover().GroundCheck(state, room, &spawn_pushout,
                                       &spawn_normal)) {
        state.position.y += spawn_pushout.y + 0.2f;
    }
    state.prev_position = state.position;
    state.grounded = controller.Mover().GroundCheck(
        state, room, nullptr, &spawn_normal);
    state.contact.was_grounded = state.grounded;
    if (state.grounded) {
        state.contact.ground_normal = spawn_normal;
        state.contact.coyote_height = state.position.y;
        state.coyote_time_remaining = config.coyote_time;
    }
    state.facing = state.target_facing = state.last_facing =
        {-old_normal.x, 0.0f, -old_normal.z};

    const Vec3 camera_forward{0.0f, 0.0f, 1.0f};
    auto tick = [&](const PlayerInput& input, bool* corner_triggered = nullptr) {
        controller.BeginMoveTick(state);
        PlayerController::StepContext context = controller.TimerInputPhase(
            state, input, camera_forward, kDt);
        controller.StatePhase(state, input, context, kDt, &room);
        controller.MovePhase(state, kDt, &room, &context);
        if (corner_triggered) *corner_triggered = context.corner_triggered;
    };
    auto sample = [&](const Vec3& before, RouteMetrics& metrics) {
        const Vec3 delta = {
            state.position.x - before.x,
            state.position.y - before.y,
            state.position.z - before.z,
        };
        metrics.max_step = std::fmax(metrics.max_step,
            std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z));
        if (state.movement_state != PlayerMovementState::Climbing) {
            ++metrics.exits;
            return;
        }
        const Vec3 feet = {
            state.position.x,
            state.position.y - config.player_half_height,
            state.position.z,
        };
        const Vec3 waist = {
            feet.x, feet.y + config.climb_waist_offset, feet.z};
        const Vec3 head = {
            feet.x, feet.y + config.climb_head_offset, feet.z};
        const WallHit waist_overlap = QueryWallPushout(
            room, waist, config.climb_pushout_dist);
        const WallHit head_overlap = QueryWallPushout(
            room, head, config.climb_pushout_dist);
        if (waist_overlap.hit) {
            metrics.max_waist_pushout = std::fmax(
                metrics.max_waist_pushout, waist_overlap.pushout);
        }
        if (head_overlap.hit) {
            // The shared resolver checks waist and head. Keep both repair
            // diagnostics alongside rendered clearance against both planes.
            metrics.max_head_probe_pushout = std::fmax(
                metrics.max_head_probe_pushout, head_overlap.pushout);
        }
        if (state.climb.corner_ease > 0.0f) {
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
            const float gap_a = Dot(normal_a, {
                feet.x - plane_a.x, feet.y - plane_a.y, feet.z - plane_a.z});
            const float gap_b = Dot(normal_b, {
                feet.x - plane_b.x, feet.y - plane_b.y, feet.z - plane_b.z});
            metrics.min_corner_feet_gap = std::fmin(metrics.min_corner_feet_gap,
                std::fmin(gap_a, gap_b));
            metrics.min_corner_normal_dot = std::fmin(metrics.min_corner_normal_dot,
                Dot(normal_a, normal_b));
            const Vec3 offset = PlayerModelCornerHorizontalOffset(
                normal_a, gap_a, normal_b, gap_b, kMadelineModelRadius,
                kMadelineModelHeight, kMadelineModelWallGap);
            metrics.max_render_offset = std::fmax(metrics.max_render_offset,
                std::sqrt(offset.x * offset.x + offset.z * offset.z));
            metrics.min_model_gap = std::fmin(metrics.min_model_gap,
                PlayerModelMinimumPlaneGapAfterTranslation(
                    normal_a, gap_a, kMadelineModelRadius,
                    kMadelineModelHeight, offset));
            metrics.min_model_gap = std::fmin(metrics.min_model_gap,
                PlayerModelMinimumPlaneGapAfterTranslation(
                    normal_b, gap_b, kMadelineModelRadius,
                    kMadelineModelHeight, offset));
            ++metrics.model_samples;
        }
    };

    PlayerInput input{};
    constexpr float kApproachX = 10.0f;
    const float kApproachZ = edge.z + config.climb_check_dist;
    const Vec3 spawn_position = state.position;
    const float start_gap = spawn_position.z - edge.z;
    float entry_gap = std::numeric_limits<float>::infinity();
    int approach_ticks = 0;
    bool entered = false;
    for (int i = 0; i < 120 && !entered; ++i) {
        const Vec3 waist = {
            state.position.x,
            state.position.y - config.player_half_height +
                config.climb_waist_offset,
            state.position.z,
        };
        const float gap = Dot(old_normal, {
            waist.x - edge.x, waist.y - edge.y, waist.z - edge.z});
        const float dx = kApproachX - state.position.x;
        const float dz = kApproachZ - state.position.z;
        const float length = std::sqrt(dx * dx + dz * dz);
        input = {};
        if (gap <= config.climb_check_dist + 0.25f &&
            std::fabs(dx) <= 1.5f) {
            entry_gap = gap;
            input.climb_held = true;
        } else if (length > 1.0e-4f) {
            // Test camera is world +Z: X input maps to world X, Y to world Z.
            input.move = {dx / length, dz / length};
        }
        tick(input);
        ++approach_ticks;
        entered = state.movement_state == PlayerMovementState::Climbing;
    }
    const float approach_distance = std::sqrt(
        (state.position.x - spawn_position.x) * (state.position.x - spawn_position.x) +
        (state.position.z - spawn_position.z) * (state.position.z - spawn_position.z));
    if (!entered || state.climb.wall_normal.z < 0.8f ||
        entry_gap > config.climb_check_dist + 0.25f ||
        std::fabs(state.position.x - kApproachX) > 2.0f ||
        approach_distance < 4.0f) {
        std::printf("FAIL: no-teleport climb entry state=%d face=%d gap=%.2f ticks=%d "
                    "approach=%.2f pos=(%.2f,%.2f,%.2f) n=(%.2f,%.2f,%.2f)\n",
                    static_cast<int>(state.movement_state), state.climb.grip_face_id,
                    entry_gap, approach_ticks, approach_distance,
                    state.position.x, state.position.y, state.position.z,
                    state.climb.wall_normal.x, state.climb.wall_normal.y,
                    state.climb.wall_normal.z);
        FreeCollMesh(mesh);
        return 1;
    }

    // OG entry resets climbInputSign. Recenter once so the opposite-facing
    // camera latches -1 before sustained side input.
    input.move = {};
    tick(input);

    // With camera forward +Z, stick left walks toward the shared edge along
    // the wall's +X tangent. Keep climb held; do not teleport between faces.
    input.move = {-1.0f, 0.0f};
    RouteMetrics metrics{};
    bool corner_seen = false;
    Vec3 target_normal{};
    for (int i = 0; i < 60; ++i) {
        const Vec3 before = state.position;
        bool corner = false;
        tick(input, &corner);
        sample(before, metrics);
        if (corner) {
            ++metrics.corner_events;
            corner_seen = true;
            target_normal = state.climb.corner_normal_to;
            metrics.adopted_target_wall =
                Dot(target_normal, state.climb.corner_normal_from) < 0.99f;
            std::printf("[corner-event] pos=(%.2f,%.2f,%.2f) to=(%.2f,%.2f,%.2f) "
                        "from_n=(%.2f,%.2f,%.2f) to_n=(%.2f,%.2f,%.2f) ease=%.3f\n",
                        state.position.x, state.position.y, state.position.z,
                        state.climb.corner_to.x, state.climb.corner_to.y,
                        state.climb.corner_to.z,
                        state.climb.corner_normal_from.x,
                        state.climb.corner_normal_from.y,
                        state.climb.corner_normal_from.z,
                        state.climb.corner_normal_to.x,
                        state.climb.corner_normal_to.y,
                        state.climb.corner_normal_to.z,
                        state.climb.corner_ease);
            // Complete easing too; a trigger alone does not prove a stable
            // destination or continued grip.
            for (int ease_tick = 0; ease_tick < 18; ++ease_tick) {
                const Vec3 ease_before = state.position;
                bool repeated_corner = false;
                tick(input, &repeated_corner);
                if (repeated_corner) ++metrics.corner_events;
                sample(ease_before, metrics);
            }
            break;
        }
    }

    std::printf("[real-map-corner-traverse] approach_ticks=%d start_gap=%.2f "
                "entry_gap=%.2f approach_distance=%.2f corner=%u target_wall=%u events=%d exits=%d "
                "max_step=%.3f waist_pushout=%.3f head_probe=%.3f model_gap=%.3f "
                "render_offset=%.2f model_samples=%d final_state=%d final_n=(%.2f,%.2f,%.2f)\n",
                approach_ticks, start_gap, entry_gap, approach_distance,
                corner_seen ? 1u : 0u, metrics.adopted_target_wall ? 1u : 0u,
                metrics.corner_events, metrics.exits, metrics.max_step,
                metrics.max_waist_pushout, metrics.max_head_probe_pushout,
                metrics.min_model_gap, metrics.max_render_offset, metrics.model_samples,
                static_cast<int>(state.movement_state),
                state.climb.wall_normal.x, state.climb.wall_normal.y,
                state.climb.wall_normal.z);
    std::printf("[corner-plane-proof] min_feet_gap=%.3f min_normal_dot=%.3f diagonal_bound=%.3f\n",
        metrics.min_corner_feet_gap, metrics.min_corner_normal_dot, kMaxRenderOffset);
    const bool eased_to_target = Dot(state.climb.wall_normal, target_normal) > 0.8f &&
                                 state.climb.corner_ease <= 0.001f;
    const bool passed = corner_seen && metrics.adopted_target_wall && eased_to_target &&
        metrics.corner_events == 1 && metrics.exits == 0 &&
        metrics.max_step <= kMaxCornerStep &&
        metrics.max_waist_pushout <= kMaxWaistPushout &&
        metrics.max_render_offset <= kMaxRenderOffset &&
        metrics.min_corner_feet_gap >= -kMaxWaistPushout &&
        metrics.min_corner_normal_dot >= -0.001f &&
        metrics.model_samples >= 2 &&
        metrics.min_model_gap >= kMadelineModelWallGap - 0.01f &&
        state.movement_state == PlayerMovementState::Climbing;
    FreeCollMesh(mesh);
    return passed ? 0 : 1;
}
