// Inc 1 (climb-wall-physics plan): motor wall-rest stability smoke test.
//
// Builds a synthetic in-memory collmesh (floor + one vertical wall) and
// verifies that a player resting adjacent to the wall:
//   1. does not drift horizontally over time,
//   2. stays grounded without vertical oscillation,
//   3. keeps valid wall flags even on zero-delta ticks (post-loop pass).
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "gameplay/physics/coll_mesh.hpp"
#include "gameplay/player/player_motor.hpp"
#include "gameplay/player/player_state.hpp"
#include "gameplay/world/world.hpp"

using namespace madeline_cube;
using namespace madeline_cube::physics;

namespace {

struct SyntheticMesh {
    CollHeader header{};
    std::vector<CollVertex> verts;
    std::vector<CollTriangle> tris;
    std::vector<CollBvhNode> nodes;
    std::vector<Vec3> world_verts;
    CollMesh mesh{};

    // Floor at y=0 plus one vertical wall quad at z=5 facing -z.
    void Build() {
        header.magic[0] = 'C'; header.magic[1] = 'M'; header.magic[2] = 'S'; header.magic[3] = 'H';
        header.version = 1;
        header.flags = 1;  // has_bvh
        header.quant_scale = 1.0f;
        header.quant_origin[0] = header.quant_origin[1] = header.quant_origin[2] = 0.0f;

        const int16_t floor_y = 0, top = 80, half = 200, wall_z = 5;
        // 0-3: floor corners; 4-7: wall corners
        verts = {
            {{-half, floor_y, -half}}, {{half, floor_y, -half}},
            {{half, floor_y, half}},   {{-half, floor_y, half}},
            {{-half, 0, wall_z}},      {{half, 0, wall_z}},
            {{half, top, wall_z}},     {{-half, top, wall_z}},
        };
        uint16_t m = MAT_SOLID;
        tris = {
            {0, 1, 2, m, 0, 0}, {0, 2, 3, m, 1, 0},
            {4, 5, 6, m, 2, 0}, {4, 6, 7, m, 3, 0},
        };
        nodes = {
            {{-half, 0, -half}, {half, top, half},
             0, static_cast<uint16_t>(tris.size())},
        };
        header.vertex_count = static_cast<uint32_t>(verts.size());
        header.triangle_count = static_cast<uint32_t>(tris.size());
        header.bvh_node_count = static_cast<uint32_t>(nodes.size());
        header.surface_link_count = 0;

        // Header must be wired before DequantVert (it reads quant params).
        mesh.header = &header;
        world_verts.resize(verts.size());
        for (size_t i = 0; i < verts.size(); ++i) {
            world_verts[i] = DequantVert(mesh, verts[i]);
        }

        mesh.vertices = verts.data();
        mesh.triangles = tris.data();
        mesh.bvh_nodes = nodes.data();
        mesh.surface_links = nullptr;
        mesh.world_verts = world_verts.data();
    }
};

constexpr float kDt = 1.0f / 60.0f;
constexpr float kGravity = 600.0f;

}  // namespace

int main() {
    SyntheticMesh syn;
    syn.Build();

    Room room;
    room.coll_mesh = &syn.mesh;
    room.collider_count = 0;
    room.player_start = {0.0f, 12.0f, 8.0f};  // 3 units from the wall plane

    PlayerMotorConfig config;
    PlayerMotor motor(config);
    PlayerState state;
    state.position = room.player_start;
    state.velocity = {};
    state.grounded = false;

    const int kTicks = 120;
    Vec3 first_rest_pos = state.position;
    int grounded_ticks = 0;
    float min_y_after_land = 1e9f, max_y_after_land = -1e9f;
    bool landed = false;
    float vy = 0.0f;

    for (int i = 0; i < kTicks; ++i) {
        if (!state.grounded) vy -= kGravity * kDt;
        MotorInput mi;
        mi.requested_velocity = {0.0f, vy, 0.0f};
        mi.wants_ground_snap = false;
        mi.wants_coyote_refresh = true;
        mi.wants_dash_refill = true;
        const MotorResult mr = motor.Step(state, room, mi, kDt);
        vy = mr.velocity.y;
        if (mr.grounded && !landed) {
            landed = true;
            first_rest_pos = state.position;
        }
        if (landed) {
            min_y_after_land = std::fmin(min_y_after_land, state.position.y);
            max_y_after_land = std::fmax(max_y_after_land, state.position.y);
        }
        if (i >= 20 && mr.grounded) ++grounded_ticks;
    }

    if (!landed) {
        printf("FAIL: never landed on floor\n");
        return 1;
    }
    const float rest_xz =
        std::fabs(state.position.x - first_rest_pos.x) +
        std::fabs(state.position.z - first_rest_pos.z);
    const float y_osc = max_y_after_land - min_y_after_land;
    printf("rest_xz=%.4f y_osc=%.4f grounded=%d/%d pos=(%.2f, %.2f, %.2f)\n",
           rest_xz, y_osc, grounded_ticks, kTicks - 20,
           state.position.x, state.position.y, state.position.z);

    if (grounded_ticks < (kTicks - 20) * 95 / 100) {
        printf("FAIL: grounded flicker near wall\n");
        return 1;
    }
    if (rest_xz >= 0.5f) {
        printf("FAIL: horizontal drift while resting against wall\n");
        return 1;
    }
    if (y_osc > 1.0f) {
        printf("FAIL: vertical oscillation while resting against wall\n");
        return 1;
    }

    // Phase 2: exact zero-delta ticks must keep wall flags alive (post-loop
    // unconditional wall contact pass).
    MotorInput zero{};
    zero.requested_velocity = {0.0f, 0.0f, 0.0f};
    bool flags_ok = true;
    for (int i = 0; i < 30; ++i) {
        const MotorResult mr = motor.Step(state, room, zero, kDt);
        if (!mr.wall_contact) flags_ok = false;
    }
    if (!flags_ok) {
        printf("FAIL: wall flags dropped on zero-delta ticks\n");
        return 1;
    }

    printf("motor_wall_rest_smoke PASS\n");
    return 0;
}
