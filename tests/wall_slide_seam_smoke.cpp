// Inc 2 (climb-and-zoom-fix plan): wall-seam slide regression.
//
// Builds a synthetic collmesh whose tall wall is TWO quads sharing a vertical
// seam, drops the player against it while falling, and verifies:
//   1. fall speed is NOT eaten at the seam (face-plane impact removal parity
//      with OG Popout — the old centre-relative diagonal normal stripped vy
//      continuously),
//   2. no popout launch through or away from the wall,
//   3. resting against the wall afterwards stays zero-net-force (drift-free,
//      wall flags alive on zero-delta ticks).
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "gameplay/physics/coll_mesh.hpp"
#include "gameplay/player/player_movement.hpp"
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

    // Floor at y=0 plus a wall at x=20 built from two quads sharing the
    // vertical seam edge at z=0 (verts 5 and 6).
    void Build() {
        header.magic[0] = 'C'; header.magic[1] = 'M'; header.magic[2] = 'S'; header.magic[3] = 'H';
        header.version = 1;
        header.flags = 1;  // has_bvh
        header.quant_scale = 1.0f;
        header.quant_origin[0] = header.quant_origin[1] = header.quant_origin[2] = 0.0f;

        const int16_t half = 200, top = 500, wall_x = 20;
        // 0-3: floor corners
        // 4-7: wall quad A (z in [-200, 0]); 5/6 shared with quad B
        // 8-9: wall quad B far corners (z in [0, 200])
        verts = {
            {{-half, 0, -half}}, {{half, 0, -half}},
            {{half, 0, half}},   {{-half, 0, half}},
            {{wall_x, 0, -200}}, {{wall_x, 0, 0}},
            {{wall_x, top, 0}},  {{wall_x, top, -200}},
            {{wall_x, 0, 200}},  {{wall_x, top, 200}},
        };
        uint16_t m = MAT_SOLID;
        tris = {
            {0, 1, 2, m, 0, 0}, {0, 2, 3, m, 1, 0},
            {4, 5, 6, m, 2, 0}, {4, 6, 7, m, 3, 0},
            {5, 8, 9, m, 4, 0}, {5, 9, 6, m, 5, 0},
        };
        nodes = {
            {{-half, 0, -half}, {half, top, half},
             0, static_cast<uint16_t>(tris.size())},
        };
        header.vertex_count = static_cast<uint32_t>(verts.size());
        header.triangle_count = static_cast<uint32_t>(tris.size());
        header.bvh_node_count = static_cast<uint32_t>(nodes.size());
        header.surface_link_count = 0;

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

    MoveService mover;
    PlayerState state;
    state.position = {16.0f, 400.0f, -30.0f};  // high enough to stay airborne 60 ticks
    state.velocity = {};
    state.grounded = false;

    // Phase 1: fall while drifting diagonally into the wall, crossing the
    // seam at z=0 mid-fall. Controller-level gravity is emulated by hand.
    float worst_ratio = 1.0f;
    bool ever_grounded = false;
    float max_x = -1e9f;
    int seam_cross_tick = -1;
    for (int i = 0; i < 60; ++i) {
        const float vy_before = state.velocity.y - kGravity * kDt;
        const float z_before = state.position.z;
        state.contact.was_grounded = state.grounded;
        state.grounded = false;
        state.velocity.x = 20.0f; state.velocity.y = vy_before; state.velocity.z = 45.0f;
        const Vec3 amount = {state.velocity.x * kDt, state.velocity.y * kDt, state.velocity.z * kDt};
        mover.SweepMove(state, room, amount, true);
        state.grounded = mover.GroundCheck(state, room, nullptr, nullptr);
        if (state.grounded) ever_grounded = true;
        if (z_before < 0.0f && state.position.z >= 0.0f && seam_cross_tick < 0) {
            seam_cross_tick = i;
        }
        max_x = std::fmax(max_x, state.position.x);
        if (!state.grounded) {
            const float expected = vy_before;  // tangential speed must survive
            if (expected < -1.0f) {
                worst_ratio = std::fmin(worst_ratio, state.velocity.y / expected);
            }
        }
    }

    printf("worst_vy_ratio=%.3f seam_cross=%d max_x=%.2f grounded=%d pos=(%.2f,%.2f,%.2f)\n",
           worst_ratio, seam_cross_tick, max_x, (int)ever_grounded,
           state.position.x, state.position.y, state.position.z);

    if (ever_grounded) {
        printf("FAIL: player hit the floor mid-test; extend wall height\n");
        return 1;
    }
    if (seam_cross_tick < 0) {
        printf("FAIL: test never crossed the wall seam\n");
        return 1;
    }
    if (max_x > 21.5f) {
        printf("FAIL: popout launched player through the wall plane\n");
        return 1;
    }
    if (worst_ratio < 0.90f) {
        printf("FAIL: fall speed damped at wall/seam (ratio %.3f)\n", worst_ratio);
        return 1;
    }

    // Phase 2: rest against the wall — zero-net-force preservation.
    Vec3 rest_start = state.position;
    bool flags_ok = true;
    for (int i = 0; i < 30; ++i) {
        state.contact.was_grounded = state.grounded;
        state.grounded = false;
        const Vec3 zero_amount = {0.0f, 0.0f, 0.0f};
        mover.SweepMove(state, room, zero_amount, false);
        state.grounded = mover.GroundCheck(state, room, nullptr, nullptr);
        if (!state.wall_contact) flags_ok = false;
    }
    const float drift =
        std::fabs(state.position.x - rest_start.x) +
        std::fabs(state.position.y - rest_start.y) +
        std::fabs(state.position.z - rest_start.z);
    printf("rest_drift=%.4f flags_ok=%d\n", drift, (int)flags_ok);
    if (!flags_ok || drift > 0.5f) {
        printf("FAIL: wall rest drifted or lost flags\n");
        return 1;
    }

    printf("wall_slide_seam_smoke PASS\n");
    return 0;
}
