// climb-animation-alignment Inc 1: reproduces the visual facing gap.
// Builds a floor + two perpendicular walls forming an outer corner,
// enters climb on the first wall, then drifts around the convex edge.
// Before the fix, state.facing stays at the old wall direction while
// state.target_facing rotates to the new wall, producing a >10° delta.
#include <cmath>
#include <cstdio>
#include <vector>

#include "gameplay/physics/coll_mesh.hpp"
#include "gameplay/player/player_controller.hpp"
#include "gameplay/player/player_state.hpp"
#include "gameplay/world/world.hpp"

using namespace madeline_cube;

namespace {

struct MeshBuilder {
    physics::CollHeader header{};
    std::vector<physics::CollVertex> verts;
    std::vector<physics::CollTriangle> tris;
    std::vector<physics::CollBvhNode> nodes;
    std::vector<Vec3> world_verts;
    physics::CollMesh mesh{};

    void Finalize() {
        header.magic[0] = 'C'; header.magic[1] = 'M'; header.magic[2] = 'S'; header.magic[3] = 'H';
        header.version = 1;
        header.flags = 1;
        header.quant_scale = 1.0f;
        header.quant_origin[0] = header.quant_origin[1] = header.quant_origin[2] = 0.0f;
        header.vertex_count = static_cast<uint32_t>(verts.size());
        header.triangle_count = static_cast<uint32_t>(tris.size());
        header.bvh_node_count = static_cast<uint32_t>(nodes.size());
        header.surface_link_count = 0;
        mesh.header = &header;
        world_verts.resize(verts.size());
        for (size_t i = 0; i < verts.size(); ++i) {
            world_verts[i] = physics::DequantVert(mesh, verts[i]);
        }
        mesh.vertices = verts.data();
        mesh.triangles = tris.data();
        mesh.bvh_nodes = nodes.data();
        mesh.surface_links = nullptr;
        mesh.world_verts = world_verts.data();
    }
};

constexpr float kDt = 1.0f / 60.0f;

struct Harness {
    Room room;
    PlayerController controller{};
    PlayerState state{};
    Vec3 cam_forward{0.0f, 0.0f, 1.0f};

    void Tick(const PlayerInput& input) {
        controller.BeginMoveTick(state);
        PlayerController::StepContext ctx =
            controller.TimerInputPhase(state, input, cam_forward, kDt);
        controller.StatePhase(state, input, ctx, kDt, &room);
        controller.MovePhase(state, kDt, &room, &ctx);
    }

    bool EnterClimb(const PlayerInput& input, int max_ticks = 30) {
        for (int i = 0; i < max_ticks; ++i) {
            Tick(input);
            if (state.movement_state == PlayerMovementState::Climbing) return true;
        }
        return false;
    }
};

inline float AngleBetweenXZ(const Vec3& a, const Vec3& b) {
    const float la = std::sqrt(a.x * a.x + a.z * a.z);
    const float lb = std::sqrt(b.x * b.x + b.z * b.z);
    if (la <= 0.0001f || lb <= 0.0001f) return 0.0f;
    const float c = (a.x * b.x + a.z * b.z) / (la * lb);
    return std::acos(std::max(-1.0f, std::min(1.0f, c)));
}

}  // namespace

int main() {
    // Inner 90° corner: wall A at x=20 (normal -x), wall B at z=20 (normal -z),
    // floor at y=0. The player climbs wall A and drifts toward +z; the side
    // ray adopts wall B abruptly via AdoptCornerWall. Before the fix,
    // target_facing flips to the new wall but facing stays stale.
    MeshBuilder mb;
    const int16_t h = 200, top = 500;
    mb.verts = {
        {{-h, 0, -h}}, {{h, 0, -h}}, {{h, 0, h}}, {{-h, 0, h}},
        {{20, 0, -200}}, {{20, 0, 200}}, {{20, top, 200}}, {{20, top, -200}},  // wall A x=20 n -x
        {{-200, 0, 20}}, {{20, 0, 20}}, {{20, top, 20}}, {{-200, top, 20}},    // wall B z=20 n -z
    };
    const uint16_t m = physics::MAT_SOLID;
    mb.tris = {
        {0, 2, 1, m, 0, 0}, {0, 3, 2, m, 1, 0},
        {4, 5, 6, m, 2, 0}, {4, 6, 7, m, 3, 0},   // wall A
        {8, 10, 9, m, 4, 0}, {8, 11, 10, m, 5, 0}, // wall B
    };
    mb.nodes = {{{-h, 0, -h}, {h, top, h}, 0, static_cast<uint16_t>(mb.tris.size())}};
    mb.Finalize();

    Harness hh;
    hh.room.coll_mesh = &mb.mesh;
    hh.room.collider_count = 0;
    // Place near wall A, facing it, at z well below the corner so there is
    // room to drift sideways before hitting the joining wall.
    hh.state.position = {16.0f, 60.0f, -40.0f};
    hh.state.grounded = false;
    hh.state.target_facing = {1.0f, 0.0f, 0.0f};
    hh.state.last_facing = {1.0f, 0.0f, 0.0f};
    hh.state.facing = {1.0f, 0.0f, 0.0f};

    PlayerInput in;
    in.climb_held = true;
    if (!hh.EnterClimb(in)) {
        printf("FAIL: never entered Climbing\n");
        return 1;
    }

    // Drift sideways along wall A toward the corner at z=20. With camera
    // (0,0,+1), stick right+up maps to world +x/+z; the wall-tangent component
    // is +z. This mirrors climb_corner_smoke Fixture A.
    hh.cam_forward = {0.0f, 0.0f, 1.0f};
    in.move = {-1.0f, 1.0f};

    float max_delta = 0.0f;
    bool wall_changed = false;
    int wall_change_tick = -1;
    Vec3 old_normal = hh.state.climb.wall_normal;
    for (int i = 0; i < 200; ++i) {
        hh.Tick(in);
        if (hh.state.movement_state != PlayerMovementState::Climbing) {
            printf("FAIL: uncommanded climb exit at tick %d\n", i);
            return 1;
        }
        const float delta = std::fabs(AngleBetweenXZ(hh.state.facing, hh.state.target_facing));
        const float delta_deg = delta * 180.0f / 3.14159265f;
        if (delta_deg > max_delta) max_delta = delta_deg;
        if (!wall_changed &&
            std::fabs(hh.state.climb.wall_normal.z + 1.0f) < 0.1f &&
            std::fabs(old_normal.z) < 0.1f) {
            wall_changed = true;
            wall_change_tick = i;
            printf("wall adopted at tick=%d facing=(%.3f,%.3f) target=(%.3f,%.3f) delta=%.2f°\n",
                   i, hh.state.facing.x, hh.state.facing.z,
                   hh.state.target_facing.x, hh.state.target_facing.z, delta_deg);
        }
    }

    printf("climb_animation_alignment_smoke: max_facing_delta=%.2f° wall_changed=%d\n",
           max_delta, (int)wall_changed);

    if (!wall_changed) {
        printf("FAIL: wall was never adopted; fixture geometry or input did not reach the corner\n");
        return 1;
    }

    // OG allows a 90-degree target change and approaches at 4pi radians/s.
    // The first tick leaves 78 degrees, then converges over subsequent ticks.
    const float expected_peak = 90.0f - 720.0f * kDt;
    if (std::fabs(max_delta - expected_peak) > 0.2f ||
        AngleBetweenXZ(hh.state.facing, hh.state.target_facing) > 0.01f) {
        printf("FAIL: facing must approach at source rate and converge (peak %.2f expected %.2f)\n", max_delta, expected_peak);
        return 1;
    }

    printf("climb_animation_alignment_smoke PASS\n");
    return 0;
}
