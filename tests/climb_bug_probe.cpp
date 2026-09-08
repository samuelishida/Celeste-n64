// memory-opt-adjacent / climb-bug probe (autoresearch): reproduces the
// device-reported climb bugs on synthetic sloped/thin walls:
//   1. "wall keeps pushing Madeline back"  — body drifts away from the wall
//   2. "going up makes it push her down"    — Y position decreases while
//      climbing up (popout Y component fights the climb)
//   3. "model wiggles around"               — per-tick position oscillation
//   4. "clips through and gets stuck"        — body ends up inside geometry
//
// Drives the REAL controller + mover (the climb_mantle_smoke harness) against
// a synthetic collmesh whose wall is configurable: vertical (baseline),
// overhang (leans toward the player, normal.y < 0), lean-away (normal.y > 0),
// and thin (2 units thick).
//
// Build:
//   g++ -std=c++17 -Isrc/user tests/climb_bug_probe.cpp \
//     src/user/gameplay/player/player_controller.cpp \
//     src/user/gameplay/player/camera_controller.cpp \
//     src/user/gameplay/player/player_movement.cpp \
//     src/user/gameplay/runtime/math.cpp \
//     src/user/gameplay/world/world.cpp \
//     src/user/gameplay/world/collectible.cpp \
//     src/user/gameplay/world/respawn_system.cpp \
//     src/user/gameplay/physics/coll_mesh.cpp \
//     src/user/gameplay/physics/geom.cpp \
//     src/user/gameplay/world/entity_dispatch.cpp \
//     src/user/gameplay/world/actor_factory.cpp \
//     src/user/gameplay/actor/moving_solid_actor.cpp \
//     -o /tmp/climb_bug_probe
#include <cmath>
#include <cstdio>
#include <vector>

#include "gameplay/physics/coll_mesh.hpp"
#include "gameplay/player/player_controller.hpp"
#include "gameplay/player/player_movement.hpp"
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

    static constexpr int16_t kHalf = 200;
    static constexpr int16_t kWallTop = 80;

    // Wall plane: base point (0,0,kWallBaseZ), normal n = (0, sin(lean), cos(lean)).
    // lean > 0 → top leans away from the player (+z side); lean < 0 → overhang.
    // thin > 0 → a second parallel wall face `thin` units behind the first.
    // reversed=true (Inc 4): the WALL faces get their triangle winding flipped
    // (the baked collision mesh is reversed — raw normals point INTO walls;
    // .agents/common-mistakes/og-map-polygon-winding.md). The floor keeps its
    // winding (GroundCheck needs the upward floor normal).
    void Build(float lean_deg, float thin = 0.0f, bool reversed = false) {
        header.magic[0] = 'C'; header.magic[1] = 'M'; header.magic[2] = 'S'; header.magic[3] = 'H';
        header.version = 1;
        header.flags = 1;
        header.quant_scale = 1.0f;
        header.quant_origin[0] = header.quant_origin[1] = header.quant_origin[2] = 0.0f;

        const float lean = lean_deg * 3.14159265f / 180.0f;
        const float ny = std::sin(lean);
        const float nz = std::cos(lean);
        // Wall "up" direction in the plane (perpendicular to n, pointing up).
        const float uy = nz;
        const float uz = -ny;
        const float kWallBaseZ = 5.0f;

        // Front face corners (x spans [-kHalf, kHalf], y spans [0, kWallTop]).
        const float fx0 = -static_cast<float>(kHalf), fx1 = static_cast<float>(kHalf);
        const float fy0 = 0.0f, fy1 = static_cast<float>(kWallTop);
        const float fz00 = kWallBaseZ + uz * fy0;  // bottom z (y=0)
        const float fz01 = kWallBaseZ + uz * fy1;  // top z (y=kWallTop)
        // Bottom edge z at x=0: base + uy*0 = base. Top edge z at x=0: base + uy*kWallTop.
        // For a plane with normal (0,ny,nz), z(y) = base + (uy/nz)*y... use the
        // plane equation: n·(p - base) = 0 → ny*y + nz*(z - base) = 0 → z = base - (ny/nz)*y.
        const float z0 = kWallBaseZ - (ny / nz) * fy0;
        const float z1 = kWallBaseZ - (ny / nz) * fy1;

        verts = {
            {{-kHalf, 0, -kHalf}}, {{kHalf, 0, -kHalf}},
            {{kHalf, 0, kHalf}},   {{-kHalf, 0, kHalf}},   // floor (y=0)
            {{static_cast<int16_t>(fx0), static_cast<int16_t>(fy0), static_cast<int16_t>(z0)}},
            {{static_cast<int16_t>(fx1), static_cast<int16_t>(fy0), static_cast<int16_t>(z0)}},
            {{static_cast<int16_t>(fx1), static_cast<int16_t>(fy1), static_cast<int16_t>(z1)}},
            {{static_cast<int16_t>(fx0), static_cast<int16_t>(fy1), static_cast<int16_t>(z1)}},
        };
        const uint16_t m = physics::MAT_SOLID;
        if (reversed) {
            // WALL faces with INVERTED winding (the bake's reversed output).
            tris = {
                {0, 1, 2, m, 0, 0}, {0, 2, 3, m, 1, 0},      // floor (keep winding)
                {4, 6, 5, m, 2, 0}, {4, 7, 6, m, 3, 0},      // wall REVERSED
            };
        } else {
            tris = {
                {0, 1, 2, m, 0, 0}, {0, 2, 3, m, 1, 0},      // floor
                {4, 5, 6, m, 2, 0}, {4, 6, 7, m, 3, 0},      // wall
            };
        }
        if (thin > 0.0f) {
            // Second wall face `thin` units behind the first (same lean).
            const float bz0 = z0 - thin;
            const float bz1 = z1 - thin;
            verts.push_back({{static_cast<int16_t>(fx0), static_cast<int16_t>(fy0), static_cast<int16_t>(bz0)}});
            verts.push_back({{static_cast<int16_t>(fx1), static_cast<int16_t>(fy0), static_cast<int16_t>(bz0)}});
            verts.push_back({{static_cast<int16_t>(fx1), static_cast<int16_t>(fy1), static_cast<int16_t>(bz1)}});
            verts.push_back({{static_cast<int16_t>(fx0), static_cast<int16_t>(fy1), static_cast<int16_t>(bz1)}});
            // Back face normal points -z-ish (away from the player).
            tris.push_back({8, 9, 10, m, 4, 0});
            tris.push_back({8, 10, 11, m, 5, 0});
        }
        nodes = {
            {{-kHalf, 0, -kHalf}, {kHalf, kWallTop, kHalf},
             0, static_cast<uint16_t>(tris.size())},
        };
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

    // Inc 4 (controls-physics-port): under-overhang fixture. A roof plate at
    // y=40 spanning z [4,24] with a 1-unit near-vertical LIP face at its edge
    // (z=8). The player stands below the plate with Z held toward the lip —
    // the overhead-clearance front-side gate must reject the grab (auto-latch
    // under an overhang must not fire).
    void BuildUnderOverhang() {
        header.magic[0] = 'C'; header.magic[1] = 'M'; header.magic[2] = 'S'; header.magic[3] = 'H';
        header.version = 1;
        header.flags = 1;
        header.quant_scale = 1.0f;
        header.quant_origin[0] = header.quant_origin[1] = header.quant_origin[2] = 0.0f;

        verts = {
            {{-kHalf, 0, -kHalf}}, {{kHalf, 0, -kHalf}},
            {{kHalf, 0, kHalf}},   {{-kHalf, 0, kHalf}},   // floor (y=0)
            {{-kHalf, 40, 4}}, {{kHalf, 40, 4}},
            {{kHalf, 40, 24}}, {{-kHalf, 40, 24}},         // roof plate (y=40)
            {{-kHalf, 40, 8}}, {{kHalf, 40, 8}},
            {{kHalf, 41, 8}}, {{-kHalf, 41, 8}},           // vertical lip face (z=8)
        };
        const uint16_t m = physics::MAT_SOLID;
        tris = {
            {0, 1, 2, m, 0, 0}, {0, 2, 3, m, 1, 0},       // floor
            {4, 5, 6, m, 2, 0}, {4, 6, 7, m, 3, 0},       // roof plate (normal up)
            {8, 9, 10, m, 4, 0}, {8, 10, 11, m, 5, 0},    // lip (normal +z)
        };
        nodes = {
            {{-kHalf, 0, -kHalf}, {kHalf, 41, 24},
             0, static_cast<uint16_t>(tris.size())},
        };
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
const Vec3 kCamForward = {0.0f, 0.0f, -1.0f};

PlayerInput MakeInput(bool climb_held, float move_y = 0.0f) {
    PlayerInput in;
    in.climb_held = climb_held;
    in.move = {0.0f, move_y};
    return in;
}

SyntheticMesh g_mesh;

struct Harness {
    Room room;
    PlayerController controller{};
    PlayerState state{};

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

    void Tick(const PlayerInput& input) {
        PlayerController::StepContext ctx =
            controller.TimerInputPhase(state, input, kCamForward, kDt);
        controller.StatePhase(state, input, ctx, kDt, &room);
        controller.MovePhase(state, kDt, &room);
    }
};

struct WallMetrics {
    float min_dist_to_plane = 1e9f;   // body-center distance from the wall plane
    float max_dist_to_plane = -1e9f;
    float min_y_delta = 1e9f;         // per-tick Y delta while climbing up
    float max_per_tick_delta = 0.0f; // max per-tick position delta (wiggle)
    float max_head_pushout = 0.0f;   // popout pushout at the head (mechanism)
    int ticks_climbing = 0;
    bool inside_geometry = false;     // body center deep inside a wall
};

// Distance from a point to the wall plane (front face): n·(p - base).
float DistToPlane(const Vec3& p, float lean_deg) {
    const float lean = lean_deg * 3.14159265f / 180.0f;
    const float ny = std::sin(lean);
    const float nz = std::cos(lean);
    return (p.x * 0.0f) + (p.y * ny) + ((p.z - 5.0f) * nz);
}

WallMetrics ClimbUp(const Harness& h, float lean_deg, int ticks) {
    WallMetrics m;
    for (int i = 0; i < ticks; ++i) {
        const Vec3 prev = h.state.position;
        // (const_cast: the harness Tick is non-const; we re-read after)
        Harness& hh = const_cast<Harness&>(h);
        hh.Tick(MakeInput(true, 1.0f));
        if (h.state.movement_state != PlayerMovementState::Climbing) break;
        ++m.ticks_climbing;
        const float d = DistToPlane(h.state.position, lean_deg);
        m.min_dist_to_plane = std::min(m.min_dist_to_plane, d);
        m.max_dist_to_plane = std::max(m.max_dist_to_plane, d);
        const float dy = h.state.position.y - prev.y;
        m.min_y_delta = std::min(m.min_y_delta, dy);
        const float dd = std::sqrt(
            (h.state.position.x - prev.x) * (h.state.position.x - prev.x) +
            (h.state.position.y - prev.y) * (h.state.position.y - prev.y) +
            (h.state.position.z - prev.z) * (h.state.position.z - prev.z));
        m.max_per_tick_delta = std::max(m.max_per_tick_delta, dd);
        // Head popout pushout (the mechanism): query at the head.
        const Vec3 head = {h.state.position.x,
                           h.state.position.y + 10.0f,
                           h.state.position.z};
        WallHit wh = QueryWallPushout(h.room, head, 3.0f);
        if (wh.hit) m.max_head_pushout = std::max(m.max_head_pushout, wh.pushout);
        // Inside geometry: body center deep inside a wall (within 0.5 of a
        // wall plane — the popout query returns the unsigned pushout, so a
        // body at the anchor (waist 3.0, center ~1.0 on sloped walls) reads
        // pushout <= 2.0 and is NOT flagged; an embedded body reads > 2.5).
        WallHit body = QueryWallPushout(h.room, h.state.position, 3.0f);
        if (body.hit && body.pushout > 2.5f) m.inside_geometry = true;
    }
    return m;
}

}  // namespace

int main() {
    int failures = 0;
    auto report = [&](bool ok, const char* what) {
        std::printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
        if (!ok) ++failures;
    };

    // --- 1. Vertical wall (baseline — should be clean) ---
    {
        g_mesh.Build(0.0f);
        Harness h;
        h.Setup(30.0f);
        if (!h.EnterClimb(MakeInput(true, 1.0f))) {
            std::printf("FAIL: vertical wall never entered Climbing\n");
            return 1;
        }
        const WallMetrics m = ClimbUp(h, 0.0f, 60);
        std::printf("vertical: ticks=%d dist=[%.2f,%.2f] minYd=%.3f maxDt=%.3f headPush=%.3f inside=%d\n",
                    m.ticks_climbing, m.min_dist_to_plane, m.max_dist_to_plane,
                    m.min_y_delta, m.max_per_tick_delta, m.max_head_pushout,
                    (int)m.inside_geometry);
        report(m.ticks_climbing >= 55, "vertical: sustained climb");
        report(m.min_y_delta > 0.0f, "vertical: no down-push while climbing up");
        report(m.max_per_tick_delta < 1.0f, "vertical: no wiggle (per-tick delta < 1)");
        report(!m.inside_geometry, "vertical: no clip");
    }

    // --- 2. Overhang wall (leans toward player, normal.y < 0) ---
    // The player spawns IN FRONT of the plane (z=15; the plane at the waist's
    // height is at z≈11.2) — OG rejects grabs from behind the plane.
    {
        g_mesh.Build(-15.0f);
        Harness h;
        h.Setup(30.0f);
        h.state.position = {0.0f, 30.0f, 15.0f};
        if (!h.EnterClimb(MakeInput(true, 1.0f))) {
            std::printf("FAIL: overhang never entered Climbing\n");
            return 1;
        }
        std::printf("overhang entry pos=(%.2f,%.2f,%.2f) wn=(%.2f,%.2f)\n",
                    h.state.position.x, h.state.position.y, h.state.position.z,
                    h.state.climb.wall_normal.x, h.state.climb.wall_normal.z);
        for (int i = 0; i < 6; ++i) {
            const Vec3 prev = h.state.position;
            h.Tick(MakeInput(true, 1.0f));
            std::printf("  t%d pos=(%.2f,%.2f,%.2f) st=%d dy=%.3f\n",
                        i, h.state.position.x, h.state.position.y, h.state.position.z,
                        (int)h.state.movement_state, h.state.position.y - prev.y);
        }
        const WallMetrics m = ClimbUp(h, -15.0f, 60);
        std::printf("overhang: ticks=%d dist=[%.2f,%.2f] minYd=%.3f maxDt=%.3f headPush=%.3f inside=%d\n",
                    m.ticks_climbing, m.min_dist_to_plane, m.max_dist_to_plane,
                    m.min_y_delta, m.max_per_tick_delta, m.max_head_pushout,
                    (int)m.inside_geometry);
        report(m.ticks_climbing >= 55, "overhang: sustained climb");
        report(m.min_y_delta > 0.0f, "overhang: no down-push while climbing up");
        report(m.max_per_tick_delta < 1.0f, "overhang: no wiggle");
        report(!m.inside_geometry, "overhang: no clip");
    }

    // --- 3. Lean-away wall (normal.y > 0) ---
    {
        g_mesh.Build(15.0f);
        Harness h;
        h.Setup(30.0f);
        if (!h.EnterClimb(MakeInput(true, 1.0f))) {
            std::printf("FAIL: lean-away never entered Climbing\n");
            return 1;
        }
        const WallMetrics m = ClimbUp(h, 15.0f, 60);
        std::printf("lean-away: ticks=%d dist=[%.2f,%.2f] minYd=%.3f maxDt=%.3f headPush=%.3f inside=%d\n",
                    m.ticks_climbing, m.min_dist_to_plane, m.max_dist_to_plane,
                    m.min_y_delta, m.max_per_tick_delta, m.max_head_pushout,
                    (int)m.inside_geometry);
        report(m.ticks_climbing >= 55, "lean-away: sustained climb");
        report(m.min_y_delta > 0.0f, "lean-away: no down-push while climbing up");
        report(m.max_per_tick_delta < 1.0f, "lean-away: no wiggle");
        report(!m.inside_geometry, "lean-away: no clip");
    }

    // --- 4. Thin wall (2 units thick) — entry snap clip risk ---
    {
        g_mesh.Build(0.0f, 2.0f);
        Harness h;
        h.Setup(30.0f);
        if (!h.EnterClimb(MakeInput(true, 1.0f))) {
            std::printf("FAIL: thin wall never entered Climbing\n");
            return 1;
        }
        const WallMetrics m = ClimbUp(h, 0.0f, 60);
        std::printf("thin: ticks=%d dist=[%.2f,%.2f] minYd=%.3f maxDt=%.3f headPush=%.3f inside=%d\n",
                    m.ticks_climbing, m.min_dist_to_plane, m.max_dist_to_plane,
                    m.min_y_delta, m.max_per_tick_delta, m.max_head_pushout,
                    (int)m.inside_geometry);
        report(m.ticks_climbing >= 55, "thin: sustained climb");
        report(m.min_y_delta > 0.0f, "thin: no down-push");
        report(m.max_per_tick_delta < 1.0f, "thin: no wiggle");
        report(!m.inside_geometry, "thin: no clip/stuck");
    }

    // --- 5. Reversed-winding vertical wall (Inc 4: the baked map's winding) ---
    // The fixture flips the wall triangle winding (raw normal points INTO the
    // wall); the player spawns IN FRONT (z=8). The OLD signed_dist<0
    // front-side gate rejected every such wall — the reason the grab never
    // fired on the real map (device: 0 [climb] START lines). The
    // winding-independent gate must still grab and sustain.
    {
        g_mesh.Build(0.0f, 0.0f, true);
        Harness h;
        h.Setup(30.0f);
        if (!h.EnterClimb(MakeInput(true, 1.0f))) {
            std::printf("FAIL: reversed-winding wall never entered Climbing\n");
            return 1;
        }
        const WallMetrics m = ClimbUp(h, 0.0f, 60);
        std::printf("reversed: ticks=%d dist=[%.2f,%.2f] inside=%d\n",
                    m.ticks_climbing, m.min_dist_to_plane, m.max_dist_to_plane,
                    (int)m.inside_geometry);
        report(m.ticks_climbing >= 55, "reversed: sustained climb on reversed winding");
        report(!m.inside_geometry, "reversed: no clip");
    }

    // --- 6. Under-overhang (Inc 4 auto-latch guard) ---
    // Player below a roof plate (y=40), Z held toward the plate's vertical lip
    // face (z=8, yspan [40,41], 4 units from the waist probe). The
    // overhead-clearance front-side gate must reject: NO climbing. This is
    // the "player under the overhang, Z held" case — grabbing a lip she is
    // under (with a ceiling over her head) would auto-latch.
    {
        g_mesh.BuildUnderOverhang();
        Harness h;
        h.Setup(45.0f);   // position.y = 45 → waist probe at 38, head at 45
        h.state.position = {0.0f, 45.0f, 12.0f};   // 4 in front of the lip (z=8)
        h.state.target_facing = {0.0f, 0.0f, -1.0f};
        h.state.last_facing = {0.0f, 0.0f, -1.0f};
        bool grabbed = false;
        for (int i = 0; i < 20; ++i) {
            h.Tick(MakeInput(true, 1.0f));
            if (h.state.movement_state == PlayerMovementState::Climbing) {
                grabbed = true;
                break;
            }
        }
        report(!grabbed, "under-overhang: NO grab (overhead clearance)");
        if (grabbed) {
            std::printf("  at pos=(%.1f,%.1f,%.1f) state=%d\n",
                        h.state.position.x, h.state.position.y, h.state.position.z,
                        (int)h.state.movement_state);
        }
    }

    // --- 7. Auto-latch guard: a wall in reach with NO shoulder input ---
    // Holding no Z (unbuffered climb), facing a wall at reach distance with no
    // move input must NOT grab — the entry requires deliberate climb_held.
    {
        g_mesh.Build(0.0f);
        Harness h;
        h.Setup(30.0f);
        bool grabbed = false;
        for (int i = 0; i < 20; ++i) {
            h.Tick(MakeInput(false, 0.0f));
            if (h.state.movement_state == PlayerMovementState::Climbing) {
                grabbed = true;
                break;
            }
        }
        report(!grabbed, "no-Z: no grab without shoulder input");
    }

    if (failures == 0) {
        std::printf("PASS: climb_bug_probe\n");
        return 0;
    }
    std::printf("%d failure(s)\n", failures);
    return 1;
}
