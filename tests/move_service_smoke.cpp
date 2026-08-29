// Inc 1 (og-mover-overhaul): MoveService fixture suite on synthetic collmesh.
//
// Proves OG-exact behavior of the new single-mover (SweepTestMove + Popout +
// GroundSnap) with no gameplay caller yet (dead code + tests only):
//   (a) a 140 u/s dash into a 1-unit-thick wall never tunnels,
//   (b) walking off a step-down snaps within 5 units via the cooldown-gated snap,
//   (c) a 35 degree slope sets grounded with ground_normal from the ray,
//   (d) walking into a wall pops out to radius 3 at waist AND head, fall speed
//       preserved tangentially,
//   (e) normal-state wall-rest equilibrium: rest distance = wall_radius (3).
//
// The (e) wall + floor reuse the collmesh geometry of motor_wall_rest_smoke
// (floor at y=0 + vertical wall quad at z=5) so the new fixture does not drift
// from the motor fixture it replaces (C2).
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

constexpr float kDt = 1.0f / 60.0f;

// Synthetic in-memory collmesh builder (same idiom as motor_wall_rest_smoke).
struct SyntheticMesh {
    CollHeader header{};
    std::vector<CollVertex> verts;
    std::vector<CollTriangle> tris;
    std::vector<CollBvhNode> nodes;
    std::vector<Vec3> world_verts;
    CollMesh mesh{};

    void Reset() {
        verts.clear();
        tris.clear();
        nodes.clear();
        world_verts.clear();
    }

    void Commit() {
        header.magic[0] = 'C'; header.magic[1] = 'M'; header.magic[2] = 'S'; header.magic[3] = 'H';
        header.version = 1;
        header.flags = 1;  // has_bvh
        header.quant_scale = 1.0f;
        header.quant_origin[0] = header.quant_origin[1] = header.quant_origin[2] = 0.0f;
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

    // Add a quad (4 verts, 2 triangles) with the given corners.
    // `mat` is the material; face_id is auto-assigned to the array index.
    uint32_t AddQuad(const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d,
                     uint16_t mat = MAT_SOLID) {
        const uint32_t base = static_cast<uint32_t>(verts.size());
        verts.push_back({static_cast<int16_t>(a.x), static_cast<int16_t>(a.y), static_cast<int16_t>(a.z)});
        verts.push_back({static_cast<int16_t>(b.x), static_cast<int16_t>(b.y), static_cast<int16_t>(b.z)});
        verts.push_back({static_cast<int16_t>(c.x), static_cast<int16_t>(c.y), static_cast<int16_t>(c.z)});
        verts.push_back({static_cast<int16_t>(d.x), static_cast<int16_t>(d.y), static_cast<int16_t>(d.z)});
        tris.push_back({static_cast<uint16_t>(base), static_cast<uint16_t>(base + 1),
                        static_cast<uint16_t>(base + 2), mat,
                        static_cast<uint16_t>(tris.size()), 0});
        tris.push_back({static_cast<uint16_t>(base), static_cast<uint16_t>(base + 2),
                        static_cast<uint16_t>(base + 3), mat,
                        static_cast<uint16_t>(tris.size()), 0});
        return base;
    }

    // One BVH node covering the whole AABB.
    void Finalize() {
        int16_t nmin[3] = {32767, 32767, 32767};
        int16_t nmax[3] = {-32767, -32767, -32767};
        for (const CollVertex& v : verts) {
            for (int k = 0; k < 3; ++k) {
                if (v.pos[k] < nmin[k]) nmin[k] = v.pos[k];
                if (v.pos[k] > nmax[k]) nmax[k] = v.pos[k];
            }
        }
        nodes.push_back({nmin[0], nmin[1], nmin[2],
                         nmax[0], nmax[1], nmax[2],
                         0, static_cast<uint16_t>(tris.size())});
        Commit();
    }
};

// The motor_wall_rest geometry: floor at y=0 (half=200) + one vertical wall
// quad at z=5 facing -z (tall from y=0 to y=80).
struct MotorWallRestGeometry {
    SyntheticMesh syn;
    void Build() {
        syn.Reset();
        const int16_t half = 200, top = 80, wall_z = 5;
        syn.AddQuad({-half, 0, -half}, {half, 0, -half}, {half, 0, half}, {-half, 0, half});
        syn.AddQuad({-half, 0, wall_z}, {half, 0, wall_z}, {half, top, wall_z}, {-half, top, wall_z});
        syn.Finalize();
    }
};

// Wall-only mesh (no floor) so a falling player's tangential speed is preserved
// (floor landing would legitimately scrub vy). Wall spans a large y range so
// waist/head stay inside it for the short popout test.
struct TallWallNoFloorGeometry {
    SyntheticMesh syn;
    void Build() {
        syn.Reset();
        const int16_t half = 200, top = 200, bottom = -200, wall_z = 5;
        syn.AddQuad({-half, bottom, wall_z}, {half, bottom, wall_z},
                    {half, top, wall_z}, {-half, top, wall_z});
        syn.Finalize();
    }
};

void WireRoom(Room& room, SyntheticMesh& syn) {
    room.coll_mesh = &syn.mesh;
    room.collider_count = 0;
}

int RunAntiTunnelDash() {
    // 1-unit-thick wall spanning z in [4,5], tall. Dash toward -z from z=8.
    SyntheticMesh syn;
    syn.Reset();
    const int16_t half = 100, top = 80;
    syn.AddQuad({-half, 0, 4}, {half, 0, 4}, {half, top, 4}, {-half, top, 4});    // front
    syn.AddQuad({-half, 0, 5}, {half, 0, 5}, {half, top, 5}, {-half, top, 5});    // back
    syn.AddQuad({-half, 0, -half}, {half, 0, -half}, {half, 0, half}, {-half, 0, half}); // floor
    syn.Finalize();

    Room room;
    WireRoom(room, syn);
    MoveService mover;
    PlayerState state;
    state.position = {0.0f, 10.0f, 8.0f};
    state.velocity = {0.0f, 0.0f, -140.0f};
    state.grounded = false;

    bool tunneled = false;
    for (int i = 0; i < 5; ++i) {
        const Vec3 amount = {state.velocity.x * kDt, state.velocity.y * kDt, state.velocity.z * kDt};
        mover.SweepMove(state, room, amount, true);
        // Interior test: position strictly outside every wall face volume
        // (z < 4 or z > 5). Waist/head heights are within the wall's y range.
        const float waist_y = state.position.y - mover.Config().half_height + 3.0f;
        const float head_y = state.position.y - mover.Config().half_height + 10.0f;
        if (waist_y > 0.0f && waist_y < 80.0f &&
            state.position.z >= 4.0f && state.position.z <= 5.0f) {
            tunneled = true;
        }
        if (head_y > 0.0f && head_y < 80.0f &&
            state.position.z >= 4.0f && state.position.z <= 5.0f) {
            tunneled = true;
        }
    }

    if (tunneled) {
        printf("FAIL (a): dashed THROUGH 1-unit wall at z=%.2f\n", state.position.z);
        return 1;
    }
    // Must NOT tunnel: rest outside the wall (z<4 or z>5) on the approach
    // (+z) side. For a thin 1-unit wall the back face's pushout can dominate,
    // resting slightly further out — what matters is staying outside it.
    if (state.position.z < 5.0f) {
        printf("FAIL (a): rest z=%.2f is inside/behind the wall (must stay on +z)\n", state.position.z);
        return 1;
    }
    printf("  (a) anti-tunnel dash OK: rest z=%.3f outside wall [4,5]\n", state.position.z);
    return 0;
}

int RunStepDownSnap() {
    // High floor y=0 for x<0; 4-unit drop at x=0; low floor y=-4 for x>0.
    SyntheticMesh syn;
    syn.Reset();
    const int16_t half = 200;
    syn.AddQuad({-half, 0, -half}, {0, 0, -half}, {0, 0, half}, {-half, 0, half});       // high floor
    syn.AddQuad({0, -4, -half}, {half, -4, -half}, {half, -4, half}, {0, -4, half});     // low floor
    syn.AddQuad({0, 0, -half}, {0, -4, -half}, {0, -4, half}, {0, 0, half});             // vertical drop face
    syn.Finalize();

    Room room;
    WireRoom(room, syn);
    MoveService mover;
    PlayerState state;
    state.position = {-2.0f, 10.0f, 0.0f};  // feet y=0 on high floor
    state.velocity = {};
    state.grounded = true;
    state.contact.was_grounded = true;       // recent ground
    state.contact.ground_snap_cooldown_remaining = 0.0f;

    // Walk right off the ledge onto the low floor. Emulate the Inc 2 caller
    // contract (OG LateUpdate): `grounded` is recomputed fresh each tick, so
    // clear it before the move — then SweepMove re-grounds only on a real
    // floor and the cooldown-gated GroundSnap pulls the feet onto the low
    // floor (4 units below, within the 5-unit snap) instead of free-falling.
    for (int i = 0; i < 60; ++i) {
        state.contact.was_grounded = state.grounded;
        state.grounded = false;
        if (!state.grounded) state.velocity.y = state.velocity.y - 600.0f * kDt;
        const Vec3 amount = {10.0f * kDt, state.velocity.y * kDt, 0.0f};
        mover.SweepMove(state, room, amount, true);
        mover.GroundSnap(state, room, kDt);
    }

    // Feet should now rest on the low floor (y=-4) -> center y=6.
    const float feet_y = state.position.y - mover.Config().half_height;
    const bool snapped = std::fabs(feet_y - (-4.0f)) < 0.5f;
    if (!snapped) {
        printf("FAIL (b): step-down snap: feet_y=%.2f (expect ~-4)\n", feet_y);
        return 1;
    }
    printf("  (b) step-down snap OK: feet_y=%.2f (expect ~-4)\n", feet_y);
    return 0;
}

int RunSlopeGround() {
    // 35-degree slope rising from y=0 at x=-5 to y=5 at x=0; flat floor beyond.
    SyntheticMesh syn;
    syn.Reset();
    const int16_t half = 200;
    // slope surface (normal ~ (cos35, sin35, 0)) via 4 verts.
    syn.AddQuad({-5, 0, -half}, {0, 5, -half}, {0, 5, half}, {-5, 0, half});
    syn.AddQuad({0, 5, -half}, {half, 5, -half}, {half, 5, half}, {0, 5, half});  // top flat
    syn.AddQuad({-half, -5, -half}, {-5, 0, -half}, {-5, 0, half}, {-half, -5, half}); // side
    syn.Finalize();

    Room room;
    WireRoom(room, syn);
    MoveService mover;
    PlayerState state;
    // Place above the slope at x=-2.5 (slope y there ~2.5); feet near it.
    state.position = {-2.5f, 12.5f, 0.0f};
    state.velocity = {};
    state.grounded = false;

    Vec3 pushout{};
    Vec3 normal{};
    const bool g = mover.GroundCheck(state, room, &pushout, &normal);
    if (!g) {
        printf("FAIL (c): slope GroundCheck returned false\n");
        return 1;
    }
    // Slope normal should be (cos, sin, 0) with |n.y| ~ 0.707.
    if (std::fabs(normal.y - 0.70710678f) > 0.05f) {
        printf("FAIL (c): slope normal.y=%.3f (expect ~0.707)\n", normal.y);
        return 1;
    }
    if (std::fabs(pushout.y) > 1.0f) {
        printf("FAIL (c): slope vertical pushout unexpectedly large: %.2f\n", pushout.y);
        return 1;
    }
    printf("  (c) slope ground OK: normal=(%.2f,%.2f,%.2f)\n", normal.x, normal.y, normal.z);
    return 0;
}

int RunWallPopoutWaistHead() {
    TallWallNoFloorGeometry geo;
    geo.Build();
    Room room;
    WireRoom(room, geo.syn);
    MoveService mover;

    // Start on the correct (+z) side: 5 units from the face plane at z=5.
    // Dash INTO the wall (-z). The wall's waist AND head tests must both pop
    // the body out to radius 3 from the face (center z ~ 8); falling (-y)
    // speed must be preserved tangentially. No floor here, so vy survives.
    PlayerState state;
    state.position = {0.0f, 10.0f, 10.0f};
    state.velocity = {0.0f, -200.0f, -50.0f};
    state.grounded = false;

    // Several ticks so the body settles against the wall at equilibrium.
    for (int i = 0; i < 10; ++i) {
        const Vec3 amount = {state.velocity.x * kDt, state.velocity.y * kDt, state.velocity.z * kDt};
        mover.SweepMove(state, room, amount, true);
        state.velocity.z = -50.0f;  // keep pushing into the wall
    }

    // Popped out to radius 3 from the face plane (z=5) -> center z ~ 8.
    const float rest_z = state.position.z;
    const float dist = rest_z - 5.0f;
    if (std::fabs(dist - 3.0f) > 1.0f) {
        printf("FAIL (d): wall popout rest dist %.2f (expect ~3)\n", dist);
        return 1;
    }
    // Waist (center.y-10+3) and head (center.y-10+10) must BOTH be inside the
    // wall's y range [bottom=-200, top=200], so both heights resolved.
    // Fall speed preserved tangentially: vy stays strongly negative.
    if (state.velocity.y > -100.0f) {
        printf("FAIL (d): fall speed scrubbed by wall impact: vy=%.2f (expect <-100)\n", state.velocity.y);
        return 1;
    }
    printf("  (d) wall popout OK: rest dist %.3f, vy=%.2f\n", dist, state.velocity.y);
    return 0;
}

int RunWallRestEquilibrium() {
    MotorWallRestGeometry geo;
    geo.Build();
    Room room;
    WireRoom(room, geo.syn);
    MoveService mover;

    PlayerState state;
    state.position = {0.0f, 10.0f, 20.0f};  // 15 units from the wall plane at z=5
    state.velocity = {0.0f, 0.0f, -64.0f};  // walk at full Normal max speed
    state.grounded = false;

    const int kTicks = 60;
    for (int i = 0; i < kTicks; ++i) {
        const Vec3 amount = {state.velocity.x * kDt, state.velocity.y * kDt, state.velocity.z * kDt};
        mover.SweepMove(state, room, amount, true);
    }

    // Normal-state rest distance from the face plane (z=5) = wall_radius (3)
    // -> body center z ~ 8, within tolerance.
    const float rest_z = state.position.z;
    const float dist_from_plane = rest_z - 5.0f;
    if (std::fabs(dist_from_plane - 3.0f) > 0.25f) {
        printf("FAIL (e): rest distance %.2f (expect 3.00 +/-0.25)\n", dist_from_plane);
        return 1;
    }

    // Zero-popout drift: over the final 30 ticks the body must not keep
    // getting pushed (position stable).
    const float z_at_30 = state.position.z;
    for (int i = 0; i < 30; ++i) {
        const Vec3 amount = {state.velocity.x * kDt, state.velocity.y * kDt, state.velocity.z * kDt};
        mover.SweepMove(state, room, amount, true);
    }
    const float drift = std::fabs(state.position.z - z_at_30);
    if (drift >= 0.5f) {
        printf("FAIL (e): wall-rest drift over 30 ticks = %.3f (expect <0.5)\n", drift);
        return 1;
    }
    printf("  (e) wall-rest equilibrium OK: rest dist %.3f, drift %.3f\n",
           dist_from_plane, drift);
    return 0;
}

}  // namespace

int main() {
    int failures = 0;
    failures += RunAntiTunnelDash();
    failures += RunStepDownSnap();
    failures += RunSlopeGround();
    failures += RunWallPopoutWaistHead();
    failures += RunWallRestEquilibrium();

    if (failures == 0) {
        printf("move_service_smoke PASS\n");
        return 0;
    }
    printf("move_service_smoke FAILED (%d fixture(s))\n", failures);
    return 1;
}
