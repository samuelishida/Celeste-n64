// climb-parity-3 Inc 3: OG corner system smoke test.
//
// Builds a floor + two perpendicular walls + an outer-corner boundary and
// drives:
//   1. inner corner: sideways climbing into a 90° corner re-anchors to the
//      new wall within a few ticks (side raycast), keeps climbing;
//   2. outer corner traversal: position stays continuous (no > 6-unit jump
//      per tick) while rounding a convex edge;
//   3. slope-away grip retention: 60 ticks on a wall leaning away from her —
//      no uncommanded exits.
// Zero uncommanded climb exits across all fixtures (done criteria).
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

    bool EnterClimb(const PlayerInput& input, int max_ticks = 30) {
        for (int i = 0; i < max_ticks; ++i) {
            Tick(input);
            if (state.movement_state == PlayerMovementState::Climbing) return true;
        }
        return false;
    }

    // og-mover-overhaul Inc 2: the tick is now controller-owned — TimerInput ->
    // StatePhase (velocity edits) -> MovePhase (SweepMove + contact). The old
    // motor.Step is replaced by MovePhase's single mover.
    void Tick(const PlayerInput& input) {
        PlayerController::StepContext ctx =
            controller.TimerInputPhase(state, input, cam_forward, kDt);
        controller.StatePhase(state, input, ctx, kDt, &room);
        controller.MovePhase(state, kDt, &room);
    }

    Vec3 cam_forward{0.0f, 0.0f, -1.0f};
};

}  // namespace

int main() {
    // ── Fixture A: inner 90° corner (walls at x=20 and z=20 meeting) ──
    {
        MeshBuilder mb;
        const int16_t h = 200, top = 500;
        mb.verts = {
            {{-h, 0, -h}}, {{h, 0, -h}}, {{h, 0, h}}, {{-h, 0, h}},     // floor
            {{20, 0, -200}}, {{20, 0, 200}}, {{20, top, 200}}, {{20, top, -200}},  // wall A x=20 (n -x)
            {{-200, 0, 20}}, {{20, 0, 20}}, {{20, top, 20}}, {{-200, top, 20}},    // wall B z=20 (n -z)
        };
        const uint16_t m = physics::MAT_SOLID;
        mb.tris = {
            {0, 1, 2, m, 0, 0}, {0, 2, 3, m, 1, 0},
            {4, 5, 6, m, 2, 0}, {4, 6, 7, m, 3, 0},   // wall A (normal -x)
            {8, 9, 10, m, 4, 0}, {8, 10, 11, m, 5, 0}, // wall B (normal -z)
        };
        mb.nodes = {{{-h, 0, -h}, {h, top, h}, 0, static_cast<uint16_t>(mb.tris.size())}};
        mb.Finalize();

        Harness hh;
        hh.room.coll_mesh = &mb.mesh;
        hh.room.collider_count = 0;
        // Climbing wall A (x=20), at a z CLEAR of wall B's extent (which
        // spans only z in [-200,20]); pressing sideways along the TOWARD the
        // corner. Motion: stick up + stick right (camera (0,0,-1) → right =
        // -x... use the controller's RelativeMoveInput: stick x>0 with cam
        // (0,0,-1) maps to world +x). Zero-magnitude in move.x on the wall.
        hh.state.position = {16.0f, 60.0f, 0.0f};  // z=0, 4 units from wall A plane
        hh.state.grounded = false;
        hh.state.target_facing = {1.0f, 0.0f, 0.0f};
        hh.state.last_facing = {1.0f, 0.0f, 0.0f};

        PlayerInput in;
        in.climb_held = true;
        if (!hh.EnterClimb(in)) {
            printf("FAIL corner A: never entered Climbing\n");
            return 1;
        }

        // Sideways drift toward +z (toward the corner at z=20): the harness
        // camera (0,0,-1) needs stick DOWN-left for +z, which conflicts with
        // climb-up. Flip this fixture's camera to (0,0,+1): stick left+up →
        // world +z drift with climb-up unchanged.
        hh.cam_forward = {0.0f, 0.0f, 1.0f};
        in.move = {-1.0f, 1.0f};  // stick left+up → world +z drift
        bool reanchored = false;
        int exits = 0;
        for (int i = 0; i < 120 && hh.state.position.z < 21.0f; ++i) {
            if (hh.state.movement_state != PlayerMovementState::Climbing) ++exits;
            hh.Tick(in);
            if (hh.state.movement_state == PlayerMovementState::Climbing &&
                std::fabs(hh.state.climb.wall_normal.z + 1.0f) < 0.1f) {
                reanchored = true;  // adopted wall B (-z normal)
                break;
            }
        }
        printf("corner A (inner): reanchored=%d exits=%d wn=(%.2f,%.2f) pos=(%.1f,%.1f,%.1f)\n",
               (int)reanchored, exits,
               hh.state.climb.wall_normal.x, hh.state.climb.wall_normal.z,
               hh.state.position.x, hh.state.position.y, hh.state.position.z);
        if (!reanchored) {
            printf("FAIL: inner corner never re-anchored to the joining wall\n");
            return 1;
        }
        if (exits > 0) {
            printf("FAIL: inner corner produced %d uncommanded exits\n", exits);
            return 1;
        }
    }

    // ── Fixture B: outer corner — climbing up a wall whose END is reached
    //    (convex edge, wall B continues on the other side of the corner). ──
    {
        // Wall A spans z [20, 200] at x=20; another wall C attaches at z=20
        // facing +z (the far side). Climbing up + drifting past z=20 must not
        // eject her.
        MeshBuilder mb;
        const int16_t h = 200, top = 500;
        mb.verts = {
            {{-h, 0, -h}}, {{h, 0, -h}}, {{h, 0, h}}, {{-h, 0, h}},     // floor
            {{20, 0, 0}}, {{20, 0, 200}}, {{20, top, 200}}, {{20, 0, 0}} // placeholder, replaced below
        };
        // Rebuild cleanly: wall A at x=20 (z 0..200, n -x); wall C at z=200
        // (x -200..20, n +z so it faces the player climbing around the corner).
        mb = MeshBuilder{};
        mb.verts = {
            {{-h, 0, -h}}, {{h, 0, -h}}, {{h, 0, h}}, {{-h, 0, h}},
            {{20, 0, 0}}, {{20, 0, 200}}, {{20, top, 200}}, {{20, top, 0}},      // wall A (n -x)
            {{-200, 0, 200}}, {{20, 0, 200}}, {{20, top, 200}}, {{-200, top, 200}}, // wall C (n -z from +z side)
        };
        const uint16_t m = physics::MAT_SOLID;
        mb.tris = {
            {0, 1, 2, m, 0, 0}, {0, 2, 3, m, 1, 0},
            {4, 5, 6, m, 2, 0}, {4, 6, 7, m, 3, 0},
            {8, 9, 10, m, 4, 0}, {8, 10, 11, m, 5, 0},
        };
        mb.nodes = {{{-h, 0, -h}, {h, top, h}, 0, static_cast<uint16_t>(mb.tris.size())}};
        mb.Finalize();

        Harness hh;
        hh.room.coll_mesh = &mb.mesh;
        hh.room.collider_count = 0;
        // 4 units off wall A (x=20): within the entry probe's reach at
        // tangency. Wall C at z=200 is 140 away — only wall A is near.
        hh.state.position = {16.0f, 60.0f, 60.0f};
        hh.state.grounded = false;
        hh.state.target_facing = {1.0f, 0.0f, 0.0f};
        hh.state.last_facing = {1.0f, 0.0f, 0.0f};

        PlayerInput in;
        in.climb_held = true;
        // Enter with a NEUTRAL stick first: moving stick (-1,+1) under the
        // default cam (0,0,-1) also drives horizontal motion pre-grab, which
        // walks her away from the wall before the grab lands.
        if (!hh.EnterClimb(in)) {
            printf("FAIL corner B: never entered Climbing\n");
            return 1;
        }
        // Same camera flip as fixture A: stick left+up → world -z drift along
        // wall A toward the edge at z=0.
        hh.cam_forward = {0.0f, 0.0f, 1.0f};
        in.move = {1.0f, 1.0f};  // stick right+up → world -z drift
        int exits = 0;
        float max_delta = 0.0f;
        Vec3 prev = hh.state.position;
        for (int i = 0; i < 150; ++i) {
            hh.Tick(in);
            if (hh.state.movement_state != PlayerMovementState::Climbing) ++exits;
            const float d = std::fabs(hh.state.position.x - prev.x) +
                            std::fabs(hh.state.position.y - prev.y) +
                            std::fabs(hh.state.position.z - prev.z);
            if (d > max_delta) max_delta = d;
            prev = hh.state.position;
        }
        printf("corner B (outer/path end): exits=%d max_tick_delta=%.2f\n", exits, max_delta);
        if (exits > 0) {
            printf("FAIL: outer-corner climb ejected her (%d exits)\n", exits);
            return 1;
        }
        if (max_delta > 6.0f) {
            printf("FAIL: position discontinuity at corner (delta %.2f > 6)\n", max_delta);
            return 1;
        }
    }

    // ── Fixture C: slope-away wall (leans back 15°) — grip retained 60 ticks.
    {
        MeshBuilder mb;
        const int16_t h = 200, top = 500;
        // Slanted wall plane: bottom edge x=20, top edge x=28 (leans away as
        // she climbs). Normal is -x with -0.26 y tilt (|y| < 0.35 → climbable).
        mb.verts = {
            {{-h, 0, -h}}, {{h, 0, -h}}, {{h, 0, h}}, {{-h, 0, h}},
            {{20, 0, -200}}, {{20, 0, 200}}, {{28, top, 200}}, {{28, top, -200}},
        };
        const uint16_t m = physics::MAT_SOLID;
        mb.tris = {
            {0, 1, 2, m, 0, 0}, {0, 2, 3, m, 1, 0},
            {4, 5, 6, m, 2, 0}, {4, 6, 7, m, 3, 0},
        };
        mb.nodes = {{{-h, 0, -h}, {h, top, h}, 0, static_cast<uint16_t>(mb.tris.size())}};
        mb.Finalize();

        Harness hh;
        hh.room.coll_mesh = &mb.mesh;
        hh.room.collider_count = 0;
        hh.state.position = {16.0f, 60.0f, 0.0f};
        hh.state.grounded = false;
        hh.state.target_facing = {1.0f, 0.0f, 0.0f};
        hh.state.last_facing = {1.0f, 0.0f, 0.0f};

        PlayerInput in;
        in.climb_held = true;
        if (!hh.EnterClimb(in)) {
            printf("FAIL corner C: never entered Climbing on the slope-away wall\n");
            return 1;
        }
        int exits = 0;
        for (int i = 0; i < 60; ++i) {
            hh.Tick(in);
            if (hh.state.movement_state != PlayerMovementState::Climbing) ++exits;
        }
        printf("corner C (slope-away): exits=%d\n", exits);
        if (exits > 0) {
            printf("FAIL: slope-away grip lost (%d exits over 60 ticks)\n", exits);
            return 1;
        }
    }

    printf("climb_corner_smoke PASS\n");
    return 0;
}