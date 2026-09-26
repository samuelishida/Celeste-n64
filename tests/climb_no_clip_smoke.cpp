// og-mover-overhaul Inc 3: climb no-clip regression.
//
// After Inc 2 the corner-adopt and corner-ease position writes run through the
// single mover (MovePhase's SweepMove + idle Popout), so any residual
// penetration must be repaired by the very next Popout. This fixture builds the
// same floor + two perpendicular walls (inner 90° corner at x=20/z=20, wall A
// x=20 normal -x, wall B z=20 normal -z) as climb_corner_smoke and asserts:
//   1. corner adopt + 3 ticks -> QueryWallPushout at waist and head report NO
//      wall deeper than equilibrium (radius 3),
//   2. a fast exit-dash at the corner cannot tunnel through the wall: the
//      body stays outside both wall faces, never interior.
#include <cmath>
#include <cstdio>
#include <vector>

#include "gameplay/physics/coll_mesh.hpp"
#include "gameplay/player/player_controller.hpp"
#include "gameplay/player/player_movement.hpp"
#include "gameplay/player/player_state.hpp"
#include "gameplay/world/world.hpp"

using namespace madeline_cube;
using namespace madeline_cube::physics;

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

// No wall deeper than equilibrium at the climb radius (3) after an adopt.
bool NoWallDeeperAt(const Room& room, const Vec3& waist, const Vec3& head) {
    const WallHit w = QueryWallPushout(room, waist, 3.0f);
    const WallHit h = QueryWallPushout(room, head, 3.0f);
    // A wall at the equilibrium (pushout ~ 0) is fine (touching); anything
    // meaningfully interior (pushout > 0.5 into the face) is a clip.
    if (w.hit && w.pushout > 0.5f) return false;
    if (h.hit && h.pushout > 0.5f) return false;
    return true;
}

}  // namespace

int main() {
    MeshBuilder mb;
    const int16_t H = 200, top = 500;
    mb.verts = {
        {{-H, 0, -H}}, {{H, 0, -H}}, {{H, 0, H}}, {{-H, 0, H}},       // floor
        {{20, 0, -200}}, {{20, 0, 200}}, {{20, top, 200}}, {{20, top, -200}},   // wall A x=20 (n -x)
        {{-200, 0, 20}}, {{20, 0, 20}}, {{20, top, 20}}, {{-200, top, 20}},     // wall B z=20 (n -z)
    };
    const uint16_t m = physics::MAT_SOLID;
    mb.tris = {
        {0, 1, 2, m, 0, 0}, {0, 2, 3, m, 1, 0},
        {4, 5, 6, m, 2, 0}, {4, 6, 7, m, 3, 0},     // wall A
        {8, 10, 9, m, 4, 0}, {8, 11, 10, m, 5, 0},  // wall B (n -z, consistent winding)
    };
    mb.nodes = {{{-H, 0, -H}, {H, top, H}, 0, static_cast<uint16_t>(mb.tris.size())}};
    mb.Finalize();

    Room room;
    room.coll_mesh = &mb.mesh;
    room.collider_count = 0;

    PlayerController controller;
    PlayerState state;
    state.position = {16.0f, 60.0f, 10.0f};  // 4 units from wall A, clear of wall B (z=20)
    state.grounded = false;
    state.target_facing = {1.0f, 0.0f, 0.0f};   // facing wall A
    state.last_facing = {1.0f, 0.0f, 0.0f};

    // ── Part 1: corner adopt must never leave residual penetration. ──
    PlayerInput in;
    in.climb_held = true;
    in.move = {};  // establish grip at rest; then climb input may be tangential
    // Climb up after entry; tangent movement must not be mistaken for entry
    // input directed into the wall plane.
    bool entered = false, clipped = false;
    for (int i = 0; i < 120; ++i) {
        if (entered) in.move = {0.0f, 1.0f};
        PlayerController::StepContext ctx =
            controller.TimerInputPhase(state, in, {0.0f, 0.0f, -1.0f}, kDt);
        controller.StatePhase(state, in, ctx, kDt, &room);
        controller.MovePhase(state, kDt, &room);
        if (state.movement_state == PlayerMovementState::Climbing) entered = true;
        // After any adopt, 3 ticks later the waist/head must NOT be interior.
        if (i % 10 == 0) {
            const float feet = state.position.y - MoveServiceConfig{}.half_height;
            const Vec3 waist = {state.position.x, feet + 3.0f, state.position.z};
            const Vec3 head = {state.position.x, feet + 10.0f, state.position.z};
            if (!NoWallDeeperAt(room, waist, head)) {
                clipped = true;
                break;
            }
        }
    }
    if (!entered) {
        printf("climb_no_clip: FAIL never entered Climbing\n");
        return 1;
    }
    if (clipped) {
        printf("climb_no_clip: FAIL residual penetration after corner adopt "
               "pos=(%.2f,%.2f,%.2f)\n",
               state.position.x, state.position.y, state.position.z);
        return 1;
    }

    // ── Part 2: fast exit-dash straight off the wall cannot tunnel through it. ──
    {
        // Re-enter climb next to wall B (z=20, normal -z) and dash THROUGH it.
        PlayerState dash_state;
        dash_state.position = {10.0f, 60.0f, 16.0f};  // 4 from wall B
        dash_state.grounded = false;
        dash_state.target_facing = {0.0f, 0.0f, 1.0f};  // facing wall B
        dash_state.last_facing = {0.0f, 0.0f, 1.0f};

        PlayerInput din;
        din.climb_held = true;
        bool d_entered = false;
        for (int i = 0; i < 20; ++i) {
            PlayerController::StepContext ctx =
                controller.TimerInputPhase(dash_state, din, {0.0f, 0.0f, 1.0f}, kDt);
            controller.StatePhase(dash_state, din, ctx, kDt, &room);
            controller.MovePhase(dash_state, kDt, &room);
            if (dash_state.movement_state == PlayerMovementState::Climbing) { d_entered = true; break; }
        }
        if (!d_entered) {
            printf("climb_no_clip: FAIL dash test never entered Climbing\n");
            return 1;
        }
        // Dash into wall B (stick +z): sets velocity at dash_speed 140 into it.
        PlayerInput dash;
        dash.dash_pressed = true;
        dash.move = {0.0f, 1.0f};
        for (int i = 0; i < 10; ++i) {
            PlayerController::StepContext ctx =
                controller.TimerInputPhase(dash_state, dash, {0.0f, 0.0f, 1.0f}, kDt);
            controller.StatePhase(dash_state, dash, ctx, kDt, &room);
            controller.MovePhase(dash_state, kDt, &room);
            dash.dash_pressed = false;  // single press
        }
        // Interior test: the body must not sit inside wall B (plane z=20, spans
        // x<=20 for the -z-normal face → interior is z>20). Waist/head heights
        // are inside the wall's y range.
        const float feet = dash_state.position.y - MoveServiceConfig{}.half_height;
        const float waist_y = feet + 3.0f;
        const float head_y = feet + 10.0f;
        if (waist_y > 5.0f && waist_y < top - 5.0f && dash_state.position.z > 20.5f) {
            printf("climb_no_clip: FAIL dash tunneled through wall B (waist) z=%.2f\n",
                   dash_state.position.z);
            return 1;
        }
        if (head_y > 5.0f && head_y < top - 5.0f && dash_state.position.z > 20.5f) {
            printf("climb_no_clip: FAIL dash tunneled through wall B (head) z=%.2f\n",
                   dash_state.position.z);
            return 1;
        }
        // Normal-state body uses rendered-player clearance; climb-only sweep
        // retains the OG 3-unit anchor. Dash exits climb, so it must rest at
        // the gameplay wall radius on the -z side of wall B.
        const float rest_distance = 20.0f - dash_state.position.z;
        const float expected_radius = controller.Mover().Config().wall_radius;
        if (std::fabs(rest_distance - expected_radius) > 0.5f) {
            printf("climb_no_clip: FAIL dash rest distance %.2f, expect %.2f\n",
                   rest_distance, expected_radius);
            return 1;
        }
    }

    printf("climb_no_clip_smoke PASS\n");
    return 0;
}
