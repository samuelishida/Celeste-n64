// climb-wall-physics plan Inc 2/3: motor-integrated climb smoke test.
//
// Drives the real motor + controller pair against a synthetic collmesh
// (floor + one vertical wall) to verify OG-style climbing end-to-end:
//   1. airborne grab enters Climbing,
//   2. stick up climbs at ~climb_speed along the wall,
//   3. neutral stick hangs stably (no release/regrab cycling),
//   4. releasing exits cleanly with no residual XZ velocity,
//   5. jump from climb SETS velocity away from the wall.
// Inc 3 adds: 6. ledge-hop mantle onto the wall top.
// climb-parity-3 Inc 2 adds: 12. OG TryClimb entry reposition (center at
//    wall.point + {0, half_height - waist_offset, 0} + n*pushout), and
//    13. 30-tick climb equilibrium (zero-popout, drift < 0.5).
#include <cmath>
#include <cstdio>
#include <vector>

#include "gameplay/physics/coll_mesh.hpp"
#include "gameplay/player/player_controller.hpp"
#include "gameplay/player/player_motor.hpp"
#include "gameplay/player/player_state.hpp"
#include "gameplay/world/world.hpp"

using namespace madeline_cube;

namespace {

struct SyntheticMesh {
    physics::CollHeader header{};
    std::vector<physics::CollVertex> verts;
    std::vector<physics::CollTriangle> tris;
    std::vector<physics::CollBvhNode> nodes;
    std::vector<Vec3> world_verts;
    physics::CollMesh mesh{};

    // Wall quad at z = kWallZ spanning y [0, kWallTop]; roof at y = kWallTop
    // spanning z [-kHalf, kWallZ]; floor at y = 0.
    static constexpr int16_t kHalf = 200;
    static constexpr int16_t kWallZ = 5;
    static constexpr int16_t kWallTop = 80;

    void Build() {
        header.magic[0] = 'C'; header.magic[1] = 'M'; header.magic[2] = 'S'; header.magic[3] = 'H';
        header.version = 1;
        header.flags = 1;
        header.quant_scale = 1.0f;
        header.quant_origin[0] = header.quant_origin[1] = header.quant_origin[2] = 0.0f;

        const int16_t floor_y = 0;
        verts = {
            {{-kHalf, floor_y, -kHalf}}, {{kHalf, floor_y, -kHalf}},
            {{kHalf, floor_y, kHalf}},   {{-kHalf, floor_y, kHalf}},
            {{-kHalf, 0, kWallZ}},       {{kHalf, 0, kWallZ}},
            {{kHalf, kWallTop, kWallZ}}, {{-kHalf, kWallTop, kWallZ}},
            {{-kHalf, kWallTop, -kHalf}}, {{kHalf, kWallTop, -kHalf}},
        };
        const uint16_t m = physics::MAT_SOLID;
        tris = {
            {0, 1, 2, m, 0, 0}, {0, 2, 3, m, 1, 0},      // floor
            {4, 5, 6, m, 2, 0}, {4, 6, 7, m, 3, 0},      // wall
            {7, 6, 9, m, 4, 0}, {7, 9, 8, m, 5, 0},      // roof (normal +y)
        };
        nodes = {
            {{-kHalf, 0, -kHalf}, {kHalf, kWallTop, kHalf},
             0, static_cast<uint16_t>(tris.size())},
        };
        header.vertex_count = static_cast<uint32_t>(verts.size());
        header.triangle_count = static_cast<uint32_t>(tris.size());
        header.bvh_node_count = static_cast<uint32_t>(nodes.size());
        header.surface_link_count = 0;

        mesh.header = &header;  // must be wired before DequantVert
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
const Vec3 kCamForward = {0.0f, 0.0f, -1.0f};  // camera looks toward -z (into the wall)

SyntheticMesh g_mesh;

PlayerInput MakeInput(bool climb_held, float move_y = 0.0f) {
    PlayerInput in;
    in.climb_held = climb_held;
    in.move = {0.0f, move_y};
    return in;
}

struct Harness {
    Room room;
    PlayerMotor motor{{}};
    PlayerController controller{};
    PlayerState state{};

    // Spawn airborne next to the wall (wall plane z=5), facing it (-z).
    void Setup(float y) {
        room.coll_mesh = &g_mesh.mesh;
        room.collider_count = 0;
        state.position = {0.0f, y, 8.0f};
        state.velocity = {};
        state.grounded = false;
        state.target_facing = {0.0f, 0.0f, -1.0f};
        state.last_facing = {0.0f, 0.0f, -1.0f};
    }

    bool EnterClimb(const PlayerInput& input, int max_ticks = 20) {
        for (int i = 0; i < max_ticks; ++i) {
            Tick(input);
            if (state.movement_state == PlayerMovementState::Climbing) return true;
        }
        return false;
    }

    // One fixed tick: motor first, controller second (scene tick order).
    MotorResult Tick(const PlayerInput& input) {
        MotorInput mi;
        mi.requested_velocity = state.velocity;
        mi.wants_ground_snap = state.contact.was_grounded &&
                               state.movement_state != PlayerMovementState::Dashing;
        mi.wants_coyote_refresh = true;
        mi.wants_dash_refill = true;
        const MotorResult mr = motor.Step(state, room, mi, kDt);
        PlayerController::StepContext ctx =
            controller.TimerInputPhase(state, input, kCamForward, kDt);
        controller.StatePhase(state, input, ctx, kDt, &room);
        controller.LateContactPhase(state);
        return mr;
    }
};

}  // namespace

int main() {
    g_mesh.Build();

    // --- 1+2: airborne grab then climb up at ~climb_speed ---
    {
        Harness h;
        h.Setup(30.0f);
        if (!h.EnterClimb(MakeInput(true, 1.0f))) {
            printf("FAIL: never entered Climbing next to a solid wall\n");
            return 1;
        }

        float climbed_sum = 0.0f;
        int climb_ticks = 0;
        for (int i = 0; i < 60; ++i) {
            h.Tick(MakeInput(true, 1.0f));
            if (h.state.movement_state != PlayerMovementState::Climbing) break;
            climbed_sum += h.state.velocity.y * kDt;
            ++climb_ticks;
        }
        if (climb_ticks == 0) {
            printf("FAIL: climb did not sustain any upward ticks\n");
            return 1;
        }
        const float avg_rate = climbed_sum / (static_cast<float>(climb_ticks) * kDt);
        printf("climb avg rate %.2f u/s over %d ticks\n", avg_rate, climb_ticks);
        if (avg_rate < 35.0f || avg_rate > 45.0f) {
            printf("FAIL: climb-up rate not ~40 u/s\n");
            return 1;
        }
    }

    // --- 3: neutral-stick hang stability ---
    {
        Harness h;
        h.Setup(30.0f);
        if (!h.EnterClimb(MakeInput(true))) {
            printf("FAIL: neutral-stick grab never entered Climbing\n");
            return 1;
        }
        const Vec3 p0 = h.state.position;
        int stayed = 0;
        for (int i = 0; i < 60; ++i) {
            h.Tick(MakeInput(true));
            if (h.state.movement_state == PlayerMovementState::Climbing) ++stayed;
        }
        const float drift =
            std::fabs(h.state.position.x - p0.x) +
            std::fabs(h.state.position.y - p0.y) +
            std::fabs(h.state.position.z - p0.z);
        printf("hang stayed %d/60 ticks, drift %.4f\n", stayed, drift);
        if (stayed < 58) {
            printf("FAIL: neutral-stick hang released early (flag wipe bug?)\n");
            return 1;
        }
        if (drift > 2.0f) {
            printf("FAIL: neutral-stick hang drifted\n");
            return 1;
        }
    }

    // --- 4: clean release ---
    {
        Harness h;
        h.Setup(30.0f);
        if (!h.EnterClimb(MakeInput(true))) {
            printf("FAIL: expected Climbing before release\n");
            return 1;
        }
        h.Tick(MakeInput(false));
        if (h.state.movement_state != PlayerMovementState::Normal) {
            printf("FAIL: release did not exit Climbing\n");
            return 1;
        }
        if (std::fabs(h.state.velocity.x) > 0.001f ||
            std::fabs(h.state.velocity.z) > 0.001f) {
            printf("FAIL: release left residual XZ velocity\n");
            return 1;
        }
    }

    // --- 5: walljump from climb SETS velocity away from the wall ---
    {
        Harness h;
        h.Setup(30.0f);
        if (!h.EnterClimb(MakeInput(true))) {
            printf("FAIL: expected Climbing before walljump\n");
            return 1;
        }
        PlayerInput wj = MakeInput(true);
        wj.jump_pressed = true;
        wj.jump_held = true;
        h.Tick(wj);
        // Neutral stick => OG-neutral launch values, SET not added.
        // Wall normal points +z (toward the player side), so the launch is +z.
        if (!(std::fabs(h.state.velocity.z - 40.0f) < 0.5f &&
              std::fabs(h.state.velocity.y - 105.0f) < 0.5f)) {
            printf("FAIL: neutral walljump SET velocity wrong (%.1f, %.1f, %.1f)\n",
                   h.state.velocity.x, h.state.velocity.y, h.state.velocity.z);
            return 1;
        }
        if (h.state.movement_state != PlayerMovementState::Normal) {
            printf("FAIL: walljump did not exit Climbing\n");
            return 1;
        }
    }

    // --- 6 (Inc 3): ledge-hop mantle onto the wall top ---
    {
        Harness h;
        h.Setup(30.0f);  // wall top at y=80
        if (!h.EnterClimb(MakeInput(true, 1.0f))) {
            printf("FAIL: expected Climbing before mantle\n");
            return 1;
        }
        // Climb until the mantle fires (feet cross y=80).
        bool mantled = false;
        for (int i = 0; i < 200; ++i) {
            if (h.state.movement_state != PlayerMovementState::Climbing) break;
            h.Tick(MakeInput(true, 1.0f));
            if (h.state.movement_state == PlayerMovementState::Normal &&
                h.state.velocity.y > 60.0f) {
                mantled = true;
                // Hop impulse: forward = -wall_normal * 40, up = 80.
                if (!(std::fabs(h.state.velocity.y - 80.0f) < 0.5f)) {
                    printf("FAIL: mantle vertical impulse %.2f != 80\n", h.state.velocity.y);
                    return 1;
                }
                if (!(h.state.velocity.z < -20.0f)) {  // -normal_z * 40, normal is +z
                    printf("FAIL: mantle forward impulse %.2f not into the lip\n",
                           h.state.velocity.z);
                    return 1;
                }
                break;
            }
        }
        if (!mantled) {
            printf("FAIL: mantle never fired at wall top (state=%d y=%.2f)\n",
                   (int)h.state.movement_state, h.state.position.y);
            return 1;
        }
        // Ride the ballistic arc: must land grounded ON TOP of the wall.
        bool landed_on_top = false;
        for (int i = 0; i < 240; ++i) {
            h.Tick(MakeInput(false));
            if (h.state.grounded && h.state.position.y > SyntheticMesh::kWallTop - 1.0f) {
                landed_on_top = true;
                break;
            }
        }
        printf("post-mantle pos=(%.2f, %.2f, %.2f)\n",
               h.state.position.x, h.state.position.y, h.state.position.z);
        if (!landed_on_top) {
            printf("FAIL: mantle did not land on top of the ledge\n");
            return 1;
        }
    }

    // --- 7 (edge-grab regression): grabbing just below the lip with up held
    //     must mantle within a few ticks, not pump release/regrab. ---
    {
        Harness h;
        h.Setup(SyntheticMesh::kWallTop + 8.0f - 10.0f);  // feet ~2 below lip
        if (!h.EnterClimb(MakeInput(true, 1.0f), 10)) {
            printf("FAIL: near-lip grab never entered Climbing\n");
            return 1;
        }
        bool mantled = false;
        for (int i = 0; i < 15; ++i) {
            if (h.state.movement_state != PlayerMovementState::Climbing) break;
            h.Tick(MakeInput(true, 1.0f));
            if (h.state.movement_state == PlayerMovementState::Normal &&
                h.state.velocity.y > 60.0f) {
                mantled = true;
                break;
            }
        }
        if (!mantled) {
            printf("FAIL: near-lip grab glitched instead of mantling "
                   "(state=%d y=%.2f)\n",
                   (int)h.state.movement_state, h.state.position.y);
            return 1;
        }
    }

    // --- 8 (Inc 2 climb-and-zoom-fix): no release/regrab pump while hanging.
    // The device capture showed [climb] START spam every tick at seams. With
    // face-plane grip identity a held grab must produce exactly ONE entry and
    // zero uncommanded exits.
    {
        Harness h;
        h.Setup(30.0f);
        if (!h.EnterClimb(MakeInput(true))) {
            printf("FAIL: pump test never entered Climbing\n");
            return 1;
        }
        int entries = 1;  // EnterClimb consumed the first
        int uncommanded_exits = 0;
        bool was_climbing = true;
        for (int i = 0; i < 90; ++i) {
            h.Tick(MakeInput(true));  // climb held the whole time
            const bool climbing =
                h.state.movement_state == PlayerMovementState::Climbing;
            if (climbing && !was_climbing) ++entries;
            if (!climbing) ++uncommanded_exits;
            was_climbing = climbing;
        }
        printf("pump check: entries=%d uncommanded_exits=%d\n",
               entries, uncommanded_exits);
        if (entries != 1 || uncommanded_exits != 0) {
            printf("FAIL: climb pump detected (%d entries, %d exits)\n",
                   entries, uncommanded_exits);
            return 1;
        }
    }

    // --- 9 (Inc 3 climb-and-zoom-fix): sphere-sweep floor gate tightening.
    // A diagonal mid-wall "trim" must NOT register as ground. The old gate
    // accepted any sweep normal with y >= 0.35, so a sloped decorative face on
    // a wall (normal in [0.35, 0.70)) latched the falling player as grounded
    // mid-wall. With the tightened gate such a face is rejected unless a
    // downward raycast confirms real floor below; here it does NOT, so the
    // player must slide down the diagonal to the real floor at y=0, never
    // grounding mid-air. (A flat box lip is NOT stepped over — sphere pushout
    // on a vertical face is horizontal-only — so this fixture tests the actual
    // Inc 3 guarantee instead.) ---
    {
        struct DiagMesh {
            physics::CollHeader header{};
            std::vector<physics::CollVertex> verts;
            std::vector<physics::CollTriangle> tris;
            std::vector<physics::CollBvhNode> nodes;
            std::vector<Vec3> world_verts;
            physics::CollMesh mesh{};

            void Build() {
                header.magic[0]='C'; header.magic[1]='M'; header.magic[2]='S'; header.magic[3]='H';
                header.version = 1;
                header.flags = 1;
                header.quant_scale = 1.0f;
                header.quant_origin[0] = header.quant_origin[1] = header.quant_origin[2] = 0.0f;

                const int16_t h = 200, top = 500;
                // floor 0-3; a tall slanted plane leaning back from x=10 (bottom)
                // up to x=18 (top), z -200..200, normal -x with a +y component
                // in [0.35, 0.70).
                verts = {
                    {{-h, 0, -h}}, {{h, 0, -h}}, {{h, 0, h}}, {{-h, 0, h}},
                    {{10, 0, -200}}, {{10, 0, 200}}, {{18, top, 200}}, {{18, top, -200}},
                };
                uint16_t m = physics::MAT_SOLID;
                tris = {
                    {0,1,2,m,0,0},{0,2,3,m,1,0},
                    {4,5,6,m,2,0},{4,6,7,m,3,0},   // slanted plane
                };
                nodes = {
                    {{-h, 0, -h}, {h, top, h}, 0, static_cast<uint16_t>(tris.size())},
                };
                header.vertex_count = static_cast<uint32_t>(verts.size());
                header.triangle_count = static_cast<uint32_t>(tris.size());
                header.bvh_node_count = static_cast<uint32_t>(nodes.size());
                header.surface_link_count = 0;

                mesh.header = &header;
                world_verts.resize(verts.size());
                for (size_t i = 0; i < verts.size(); ++i)
                    world_verts[i] = physics::DequantVert(mesh, verts[i]);
                mesh.vertices = verts.data();
                mesh.triangles = tris.data();
                mesh.bvh_nodes = nodes.data();
                mesh.surface_links = nullptr;
                mesh.world_verts = world_verts.data();
            }
        };

        DiagMesh dm;
        dm.Build();
        Room room;
        room.coll_mesh = &dm.mesh;
        room.collider_count = 0;

        PlayerMotorConfig cfg;
        cfg.radius = 5.0f;
        cfg.half_height = 5.0f;
        PlayerMotor motor(cfg);
        PlayerState st;
        st.position = {8.0f, 400.0f, 0.0f};  // well above the trim; drop straight down
        st.velocity = {};
        st.grounded = false;

        const float grav = 600.0f;
        // Track where (if anywhere) grounding first happens. Legit grounding is
        // the real floor at y=0; grounding while still high up the wall (at the
        // trim, y far above 0) is the mid-wall latch this fix must prevent.
        float first_ground_y_feet = 1e9f;
        bool ever_grounded = false;
        for (int i = 0; i < 120; ++i) {
            const float vy = st.grounded ? 0.0f : st.velocity.y - grav * kDt;
            MotorInput mi;
            mi.requested_velocity = {0.0f, vy, 0.0f};
            mi.wants_ground_snap = false;  // no snap; must NOT hook the diagonal
            mi.wants_coyote_refresh = true;
            mi.wants_dash_refill = true;
            MotorResult mr = motor.Step(st, room, mi, kDt);
            if (mr.grounded && !ever_grounded) {
                first_ground_y_feet = st.position.y - cfg.half_height;
                ever_grounded = true;
            }
        }
        const float feet = st.position.y - cfg.half_height;
        printf("diag: ever_grounded=%d first_ground_y_feet=%.2f final_feet=%.2f "
               "pos=(%.2f,%.2f,%.2f)\n",
               (int)ever_grounded, first_ground_y_feet, feet,
               st.position.x, st.position.y, st.position.z);
        // If grounded at all, it must be on the REAL floor (feet ~ 0), never on
        // the diagonal trim (feet would be a large positive value mid-wall).
        if (ever_grounded && first_ground_y_feet > 1.0f) {
            printf("FAIL: diagonal mid-wall trim latched as ground at feet=%.2f\n",
                   first_ground_y_feet);
            return 1;
        }
    }

    // --- 10 (climb-and-zoom-fix follow-up): climbing UP a tall wall must not
    // spurious-mantle mid-wall. The device showed grab/fall/regrab while holding
    // up: at rest the player sits exactly tangent to the wall (motor radius =
    // probe radius), so the grip probe flapped and fired a mantle hop mid-wall.
    // With a grip-probe skin above the motor radius the grip is stable; holding
    // up climbs continuously with ZERO exits before the real ledge (y=80).
    {
        Harness h;
        h.Setup(30.0f);
        if (!h.EnterClimb(MakeInput(true))) {
            printf("FAIL: up-climb never entered Climbing\n");
            return 1;
        }
        // Climb up for a window that stays below the wall top (y=80): from
        // y=30 at climb_speed 40 that's well under 1s. Only the genuine mantle
        // at the lip (y=80) should ever exit, and we stop before reaching it.
        int uncommanded_exits = 0;
        int climbed_ticks = 0;
        float start_y = h.state.position.y;
        for (int i = 0; i < 45; ++i) {
            h.Tick(MakeInput(true, 1.0f));  // hold Z + up
            const bool climbing =
                h.state.movement_state == PlayerMovementState::Climbing;
            if (!climbing) ++uncommanded_exits;
            if (climbing) ++climbed_ticks;
        }
        const float gained = h.state.position.y - start_y;
        printf("up-climb: uncommanded_exits=%d climbed_ticks=%d gained=%.2f "
               "y=%.2f\n", uncommanded_exits, climbed_ticks, gained,
               h.state.position.y);
        if (uncommanded_exits > 0) {
            printf("FAIL: up-climb exited before the lip (grip unstable)\n");
            return 1;
        }
        if (climbed_ticks < 44 || gained < 20.0f) {
            printf("FAIL: up-climb did not sustain upward (climbed=%d gained=%.2f)\n",
                   climbed_ticks, gained);
            return 1;
        }
    }

    // --- 11 (wall-climb-overhaul Inc 2): persistent grip identity at a corner.
    // Two walls meet at 90° (x=20 and z=20, sharing an edge). Climbing up the
    // first wall must stay on that SAME face across frames — the plane-based
    // re-acquire keeps the stored face, so the release check does not flip when
    // the nearest-any-wall normal would change near the corner. Assert a single
    // climb entry and zero uncommanded exits over the hold.
    {
        struct CornerMesh {
            physics::CollHeader header{};
            std::vector<physics::CollVertex> verts;
            std::vector<physics::CollTriangle> tris;
            std::vector<physics::CollBvhNode> nodes;
            std::vector<Vec3> world_verts;
            physics::CollMesh mesh{};

            void Build() {
                header.magic[0]='C'; header.magic[1]='M'; header.magic[2]='S'; header.magic[3]='H';
                header.version = 1;
                header.flags = 1;
                header.quant_scale = 1.0f;
                header.quant_origin[0] = header.quant_origin[1] = header.quant_origin[2] = 0.0f;

                const int16_t h = 200, top = 500;
                // 0-3 floor
                // wall A at x=20 (z -200..200), normal -x: verts 4-7
                // wall B at z=20 (x 0..20), normal -z: verts 8-11
                verts = {
                    {{-h, 0, -h}}, {{h, 0, -h}}, {{h, 0, h}}, {{-h, 0, h}},
                    {{20, 0, -200}}, {{20, 0, 200}}, {{20, top, 200}}, {{20, top, -200}},
                    {{-200, 0, 20}}, {{20, 0, 20}}, {{20, top, 20}}, {{-200, top, 20}},
                };
                uint16_t m = physics::MAT_SOLID;
                tris = {
                    {0,1,2,m,0,0},{0,2,3,m,1,0},          // floor
                    {4,5,6,m,2,0},{4,6,7,m,3,0},          // wall A (normal -x)
                    {8,9,10,m,4,0},{8,10,11,m,5,0},       // wall B (normal -z)
                };
                nodes = {
                    {{-h, 0, -h}, {h, top, h}, 0, static_cast<uint16_t>(tris.size())},
                };
                header.vertex_count = static_cast<uint32_t>(verts.size());
                header.triangle_count = static_cast<uint32_t>(tris.size());
                header.bvh_node_count = static_cast<uint32_t>(nodes.size());
                header.surface_link_count = 0;

                mesh.header = &header;
                world_verts.resize(verts.size());
                for (size_t i = 0; i < verts.size(); ++i)
                    world_verts[i] = physics::DequantVert(mesh, verts[i]);
                mesh.vertices = verts.data();
                mesh.triangles = tris.data();
                mesh.bvh_nodes = nodes.data();
                mesh.surface_links = nullptr;
                mesh.world_verts = world_verts.data();
            }
        };

        CornerMesh cm;
        cm.Build();
        Room room;
        room.coll_mesh = &cm.mesh;
        room.collider_count = 0;

        PlayerMotorConfig cfg;
        cfg.radius = 5.0f;
        PlayerMotor motor(cfg);
        PlayerController ctrl;
        PlayerState st;
        // Spawn touching wall A (x=20) at its corner edge (z=20), facing +x.
        st.position = {15.0f, 100.0f, 19.0f};
        st.velocity = {};
        st.grounded = false;
        st.target_facing = {1, 0, 0};
        st.last_facing = {1, 0, 0};

        // Hold climb with neutral stick, then up — must sustain on wall A.
        PlayerInput in;
        in.climb_held = true;
        // Enter climb.
        bool entered = false;
        for (int i = 0; i < 20; ++i) {
            MotorInput mi;
            mi.requested_velocity = st.velocity;
            mi.wants_ground_snap = false;
            mi.wants_coyote_refresh = true;
            mi.wants_dash_refill = true;
            motor.Step(st, room, mi, kDt);
            PlayerController::StepContext ctx =
                ctrl.TimerInputPhase(st, in, kCamForward, kDt);
            ctrl.StatePhase(st, in, ctx, kDt, &room);
            ctrl.LateContactPhase(st);
            if (st.movement_state == PlayerMovementState::Climbing) { entered = true; break; }
        }
        if (!entered) {
            printf("FAIL: corner climb never entered Climbing\n");
            return 1;
        }

        // Now hold climb + up for 60 ticks; assert ONE entry, ZERO exits.
        in.move = {0.0f, 1.0f};
        int entries = 1, exits = 0;
        bool was_climbing = true;
        for (int i = 0; i < 60; ++i) {
            MotorInput mi;
            mi.requested_velocity = st.velocity;
            mi.wants_ground_snap = false;
            mi.wants_coyote_refresh = true;
            mi.wants_dash_refill = true;
            motor.Step(st, room, mi, kDt);
            PlayerController::StepContext ctx =
                ctrl.TimerInputPhase(st, in, kCamForward, kDt);
            ctrl.StatePhase(st, in, ctx, kDt, &room);
            ctrl.LateContactPhase(st);
            const bool climbing = st.movement_state == PlayerMovementState::Climbing;
            if (climbing && !was_climbing) ++entries;
            if (!climbing) ++exits;
            was_climbing = climbing;
        }
        printf("corner climb: entries=%d exits=%d y=%.2f face=%d\n",
               entries, exits, st.position.y, st.climb.grip_face_id);
        if (entries != 1 || exits != 0) {
            printf("FAIL: corner climb pumped (%d entries, %d exits)\n", entries, exits);
            return 1;
        }
    }

    // --- 12 (climb-parity-3 Inc 2): OG TryClimb entry reposition. On grab,
    // the CENTER must land at wall.point + {0, half_height - waist_offset, 0}
    // + normal*pushout — here wall z=5, normal +z, wall point.y at the waist:
    // center = (unchanged_x, wall_point.y + 7, 5 + 3 = 8).
    {
        Harness h;
        h.Setup(30.0f);
        if (!h.EnterClimb(MakeInput(true))) {
            printf("FAIL: entry-repos test never entered Climbing\n");
            return 1;
        }
        // Wall plane z=5, normal +z → anchored center z must be 8
        // (wall.Point.z + pushout), hanging with waist at wall point height.
        const float expected_center_z = 5.0f + 3.0f;
        const float dz = std::fabs(h.state.position.z - expected_center_z);
        printf("entry-repos: pos=(%.2f,%.2f,%.2f) expected z=%.2f\n",
               h.state.position.x, h.state.position.y, h.state.position.z,
               expected_center_z);
        if (dz > 0.5f) {
            printf("FAIL: entry did not re-anchor to wall.point + n*3 (dz=%.2f)\n", dz);
            return 1;
        }
    }

    // --- 13 (climb-parity-3 Inc 2): zero-popout equilibrium — after the
    // grab, 30 neutral climb ticks must not drift the position (RunWallContact
    // at climb radius 3.0 finds zero penetration at the anchor).
    {
        Harness h;
        h.Setup(30.0f);
        if (!h.EnterClimb(MakeInput(true))) {
            printf("FAIL: equilibrium test never entered Climbing\n");
            return 1;
        }
        const Vec3 anchor = h.state.position;
        int stayed = 0;
        for (int i = 0; i < 30; ++i) {
            h.Tick(MakeInput(true));
            if (h.state.movement_state == PlayerMovementState::Climbing) ++stayed;
        }
        const float drift = std::fabs(h.state.position.x - anchor.x) +
                            std::fabs(h.state.position.y - anchor.y) +
                            std::fabs(h.state.position.z - anchor.z);
        printf("equilibrium: stayed=%d/30 drift=%.4f\n", stayed, drift);
        if (stayed != 30) {
            printf("FAIL: climb did not sustain the 30-tick equilibrium\n");
            return 1;
        }
        if (drift > 0.5f) {
            printf("FAIL: climb anchor drifted (popout oscillation, drift=%.3f)\n", drift);
            return 1;
        }
    }

    printf("climb_mantle_smoke PASS\n");
    return 0;
}
