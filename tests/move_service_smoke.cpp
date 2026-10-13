// MoveService fixture suite on synthetic collmesh. The controller-level lip
// fixture exercises the same mover through actual input/state/move ticks.
// Proves the single-mover (SweepTestMove + Popout + GroundSnap) behavior:
//   (a) a 140 u/s dash into a 1-unit-thick wall never tunnels,
//   (b) walking off a step-down snaps within 5 units via the cooldown-gated snap,
//   (c) a 35 degree slope sets grounded with ground_normal from the ray,
//   (d) walking into a wall pops out to radius 3 at waist AND head, fall speed
//       preserved tangentially,
//   (e) normal-state wall-rest equilibrium: rest distance = wall_radius (3).
// Other checks cover ceiling anchoring, stale wall-contact clearing,
// physics/model clearance, and controller-driven step-up over a 5-unit lip
// while rejecting 6-unit steps and low roofs.
//
// The (e) wall + floor reuse the collmesh geometry of motor_wall_rest_smoke
// (floor at y=0 + vertical wall quad at z=5) so the new fixture does not drift
// from the motor fixture it replaces (C2).
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "gameplay/physics/coll_mesh.hpp"
#include "gameplay/player/player_controller.hpp"
#include "gameplay/player/player_movement.hpp"
#include "gameplay/player/player_state.hpp"
#include "gameplay/player/player_model_clearance.hpp"
#include "gameplay/world/world.hpp"

using namespace madeline_cube;
using namespace madeline_cube::physics;

namespace {

void TickMove(const MoveService& mover, PlayerState& state, const Room& room,
              const Vec3& delta, bool resolve) {
    mover.BeginTick(state);
    mover.SweepMove(state, room, delta, resolve);
    mover.Popout(state, room, false);
    state.grounded = mover.GroundCheck(state, room, nullptr, nullptr);
}


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
                     uint16_t mat = MAT_SOLID, Vec3 authored_normal = {0,1,0}) {
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
        const Vec3 raw = Cross(Sub(b,a),Sub(c,a));
        if (Dot(raw,authored_normal) < 0) {
            std::swap(tris[tris.size()-2].i1,tris[tris.size()-2].i2);
            std::swap(tris[tris.size()-1].i1,tris[tris.size()-1].i2);
        }
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

struct CeilingOnlyGeometry {
    SyntheticMesh syn;
    void Build() {
        syn.Reset();
        const int16_t half = 200;
        constexpr int16_t ceiling_y = 20;
        syn.AddQuad({-half, ceiling_y, -half}, {half, ceiling_y, -half},
                    {half, ceiling_y, half}, {-half, ceiling_y, half}, MAT_SOLID, {0,-1,0});
        syn.Finalize();
    }
};

struct KneeHighLipGeometry {
    SyntheticMesh syn;
    void Build(int16_t lip_height = 5, int16_t ceiling_y = 0) {
        syn.Reset();
        constexpr int16_t span = 100;
        const float lip_y = static_cast<float>(lip_height);
        syn.AddQuad({-span, 0, -span}, {0, 0, -span},
                    {0, 0, span}, {-span, 0, span});
        syn.AddQuad({0, 0, -span}, {0, 0, span},
                    {0, lip_y, span}, {0, lip_y, -span});
        syn.AddQuad({0, lip_y, -span}, {span, lip_y, -span},
                    {span, lip_y, span}, {0, lip_y, span});
        if (ceiling_y > 0) {
            const float roof_y = static_cast<float>(ceiling_y);
            syn.AddQuad({0, roof_y, -span}, {span, roof_y, -span},
                        {span, roof_y, span}, {0, roof_y, span}, MAT_SOLID, {0,-1,0});
        }
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
    syn.AddQuad({-half, 0, 4}, {half, 0, 4}, {half, top, 4}, {-half, top, 4}, MAT_SOLID, {0,0,-1});    // front
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
        TickMove(mover, state, room, amount, true);
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
        TickMove(mover, state, room, amount, true);
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

int RunDeathContactAccumulatesAcrossSweep() {
    SyntheticMesh syn;
    syn.Reset();
    constexpr int16_t half = 100;
    syn.AddQuad({-half, 0, -half}, {0, 0, -half},
                {0, 0, half}, {-half, 0, half});
    syn.AddQuad({0, 0, -half}, {2, 0, -half},
                {2, 0, half}, {0, 0, half}, MAT_SOLID | MAT_DEATH);
    syn.AddQuad({2, 0, -half}, {half, 0, -half},
                {half, 0, half}, {2, 0, half});
    syn.Finalize();

    Room room;
    WireRoom(room, syn);
    MoveService mover;
    PlayerState state;
    state.position = {-1.0f, mover.Config().half_height, 0.0f};
    state.grounded = true;
    TickMove(mover, state, room, {8.0f, 0.0f, 0.0f}, true);

    if (state.position.x <= 2.0f || !state.death_contact) {
        printf("FAIL (death sweep): x=%.2f death_contact=%u; crossed death strip but lost contact\n",
               state.position.x, state.death_contact ? 1u : 0u);
        return 1;
    }
    TickMove(mover, state, room, {}, false);
    if (state.death_contact) {
        printf("FAIL (death sweep): contact stayed latched into next safe move\n");
        return 1;
    }
    printf("  (death sweep) contact accumulates across substeps OK\n");
    return 0;
}

int RunClimbCancelsGroundSnapWhileSustaining() {
    MotorWallRestGeometry geo;
    geo.Build();
    Room room;
    WireRoom(room, geo.syn);
    MovementConfig config;
    PlayerController controller(config);
    PlayerState state;
    state.position = {0.0f, config.player_half_height, 8.0f};
    state.grounded = true;
    state.contact.was_grounded = true;
    state.contact.ground_normal = {0.0f, 1.0f, 0.0f};
    state.last_facing = {0.0f, 0.0f, -1.0f};
    state.facing = state.target_facing = state.last_facing;

    PlayerInput input;
    input.move = {0.0f, 1.0f};
    input.climb_held = true;
    const Vec3 camera_forward = {0.0f, 0.0f, -1.0f};

    controller.Step(state, input, camera_forward, kDt, &room);
    if (state.movement_state != PlayerMovementState::Climbing || !state.grounded) {
        printf("FAIL (climb snap): setup did not enter grounded climb: state=%d grounded=%u\n",
               static_cast<int>(state.movement_state), state.grounded ? 1u : 0u);
        return 1;
    }
    const float climb_entry_y = state.position.y;

    // The first uphill climb tick leaves floor support. OG cancels ground snap
    // in every held StClimbingUpdate; without that refresh, MovePhase sees the
    // previous tick grounded and pulls this 0.667-unit climb step back down.
    controller.Step(state, input, camera_forward, kDt, &room);
    const float climb_step = state.position.y - climb_entry_y;
    if (state.movement_state != PlayerMovementState::Climbing ||
        climb_step < 0.5f ||
        state.contact.ground_snap_cooldown_remaining <= 0.0f) {
        printf("FAIL (climb snap): uphill step erased: dy=%.3f grounded=%u snap_cd=%.3f\n",
               climb_step, state.grounded ? 1u : 0u,
               state.contact.ground_snap_cooldown_remaining);
        return 1;
    }
    printf("  (climb snap) held climb rises %.3f without floor snap\n", climb_step);
    return 0;
}

int RunControllerStepDownGroundedState(uint16_t lower_floor_material) {
    // A one-tick walk off a 4-unit drop is within OG ground-snap range. The
    // controller must publish the snapped contact as grounded in the SAME
    // tick; otherwise the next state update incorrectly applies air control.
    SyntheticMesh syn;
    syn.Reset();
    constexpr int16_t half = 200;
    syn.AddQuad({-half, 0, -half}, {0, 0, -half},
                {0, 0, half}, {-half, 0, half});
    syn.AddQuad({0, -4, -half}, {half, -4, -half},
                {half, -4, half}, {0, -4, half}, lower_floor_material);
    syn.Finalize();

    Room room;
    WireRoom(room, syn);
    MovementConfig config;
    PlayerController controller(config);
    PlayerState state;
    state.position = {-0.05f, config.player_half_height, 0.0f};
    state.velocity = {64.0f, 0.0f, 0.0f};
    state.grounded = true;
    state.contact.was_grounded = true;
    state.contact.ground_normal = {0.0f, 1.0f, 0.0f};
    state.contact.ground_snap_cooldown_remaining = 0.0f;
    state.auto_jump = true;

    PlayerInput input;
    input.jump_held = true;
    controller.Step(state, input, {0.0f, 0.0f, 1.0f}, kDt, &room);

    const float feet_y = state.position.y - config.player_half_height;
    const bool expected_death = (lower_floor_material & MAT_DEATH) != 0;
    if (std::fabs(feet_y - (-4.0f)) > 0.01f || !state.grounded ||
        !state.contact.was_grounded || state.auto_jump ||
        state.death_contact != expected_death ||
        std::fabs(state.coyote_time_remaining - config.coyote_time) > 0.001f) {
        printf("FAIL (controller step-down): feet=%.2f grounded=%u was_grounded=%u auto_jump=%u death=%u coyote=%.3f\n",
               feet_y, state.grounded ? 1u : 0u,
               state.contact.was_grounded ? 1u : 0u, state.auto_jump ? 1u : 0u,
               state.death_contact ? 1u : 0u, state.coyote_time_remaining);
        return 1;
    }
    printf("  (controller step-down) snap contact grounded/death=%u OK\n",
           state.death_contact ? 1u : 0u);
    return 0;
}

int RunKneeHighLipStepUp() {
    KneeHighLipGeometry geo;
    geo.Build(2);
    Room room;
    WireRoom(room, geo.syn);
    MoveService mover;
    const float half_height = mover.Config().half_height;
    constexpr float lip_height = 2.0f;

    PlayerState state;
    state.position = {-6.0f, half_height, 0.0f};
    state.velocity = {64.0f, 0.0f, 0.0f};
    state.grounded = true;
    for (int i = 0; i < 30; ++i) {
        TickMove(mover, state, room, {state.velocity.x * kDt, 0.0f, 0.0f}, true);
        state.grounded = mover.GroundCheck(state, room, nullptr, nullptr);
    }

    const float feet_y = state.position.y - half_height;
    if (state.position.x < 3.0f || !state.grounded ||
        std::fabs(feet_y - lip_height) > 0.5f) {
        printf("FAIL (lip): blocked at x=%.2f feet_y=%.2f grounded=%u; expected walkover to top y=%.1f\n",
               state.position.x, feet_y, state.grounded ? 1u : 0u, lip_height);
        return 1;
    }
    printf("  (lip) knee-high step-up OK: x=%.2f feet_y=%.2f\n", state.position.x, feet_y);
    return 0;
}

int RunLipStepRejectsUnsafeRise() {
    int failures = 0;
    const int16_t lip_heights[] = {6, 5};
    const int16_t ceiling_heights[] = {0, 15};
    const char* cases[] = {"too-high lip", "low overhang"};
    for (int i = 0; i < 2; ++i) {
        KneeHighLipGeometry geo;
        geo.Build(lip_heights[i], ceiling_heights[i]);
        Room room;
        WireRoom(room, geo.syn);
        MoveService mover;
        const float half_height = mover.Config().half_height;
        PlayerState state;
        state.position = {-6.0f, half_height, 0.0f};
        state.velocity = {64.0f, 0.0f, 0.0f};
        state.grounded = true;
        for (int tick = 0; tick < 12; ++tick) {
            TickMove(mover, state, room,
                {state.velocity.x * kDt, 0.0f, 0.0f}, true);
            state.grounded = mover.GroundCheck(state, room, nullptr, nullptr);
        }
        const float feet_y = state.position.y - half_height;
        if (state.position.x > -2.5f || feet_y > 0.5f) {
            printf("FAIL (lip): %s was stepped through: x=%.2f feet_y=%.2f\n",
                   cases[i], state.position.x, feet_y);
            ++failures;
        } else {
            printf("  (lip) rejects %s OK: x=%.2f feet_y=%.2f\n",
                   cases[i], state.position.x, feet_y);
        }
    }
    return failures;
}

int RunControllerWalksKneeHighLip() {
    KneeHighLipGeometry geo;
    geo.Build(2);
    Room room;
    WireRoom(room, geo.syn);
    MovementConfig config;
    PlayerController controller(config);
    PlayerState state;
    state.position = {-6.0f, config.player_half_height, 0.0f};
    state.grounded = true;
    state.contact.was_grounded = true;
    state.contact.ground_normal = {0.0f, 1.0f, 0.0f};

    PlayerInput input;
    input.move = {1.0f, 0.0f};  // camera-relative +X
    const Vec3 camera_forward = {0.0f, 0.0f, 1.0f};
    for (int tick = 0; tick < 45; ++tick) {
        controller.Step(state, input, camera_forward, kDt, &room);
    }

    const float feet_y = state.position.y - config.player_half_height;
    if (state.position.x < 3.0f || !state.grounded ||
        std::fabs(feet_y - 2.0f) > 0.5f) {
        printf("FAIL (controller lip): x=%.2f feet_y=%.2f grounded=%u v_x=%.2f\n",
               state.position.x, feet_y, state.grounded ? 1u : 0u, state.velocity.x);
        return 1;
    }
    printf("  (controller lip) input-driven walkover OK: x=%.2f feet_y=%.2f\n",
           state.position.x, feet_y);
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
    state.position = {-2.5f, 2.5f + mover.Config().half_height, 0.0f};
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

int RunCeilingPopoutUsesFeetAnchor() {
    // OG Position is the feet anchor. CeilingCheck snaps that feet position
    // to roof - 12; demake PlayerState stores center position, so the mover
    // must add half_height back when applying the OG result.
    CeilingOnlyGeometry geo;
    geo.Build();
    Room room;
    WireRoom(room, geo.syn);
    MoveService mover;
    const float half_height = mover.Config().half_height;
    constexpr float ceiling_y = 20.0f;
    constexpr float expected_feet_y = ceiling_y - 12.0f;

    PlayerState state;
    state.position = {0.0f, 10.0f + half_height, 0.0f};  // feet y=10; ceiling overhead
    state.velocity.y = 30.0f;
    if (!mover.CeilingCheck(state, room, nullptr)) {
        printf("FAIL (ceiling): expected ceiling hit\n");
        return 1;
    }
    mover.Popout(state, room, true);

    const float feet_y = state.position.y - half_height;
    if (std::fabs(feet_y - expected_feet_y) > 0.01f || state.velocity.y != 0.0f ||
        !state.ceiling_contact) {
        printf("FAIL (ceiling): feet y=%.2f expected %.2f; vy=%.2f ceil=%u\n",
               feet_y, expected_feet_y, state.velocity.y,
               state.ceiling_contact ? 1u : 0u);
        return 1;
    }
    printf("  (ceiling) feet-anchored popout OK: feet y=%.2f\n", feet_y);
    TickMove(mover, state, room, {0.0f, -1.0f, 0.0f}, false);
    if (state.ceiling_contact) {
        printf("FAIL (ceiling): contact remained latched after moving clear\n");
        return 1;
    }
    return 0;
}

int RunPopoutMixedGroundCeilingWallOrder() {
    // Deliberately overlapping floor/roof force OG's ground-if / ceiling-else
    // branch to choose ground. The wall remains a separate following if and
    // must still repair position/project horizontal impact in that same call.
    SyntheticMesh syn;
    syn.Reset();
    constexpr int16_t half = 100;
    syn.AddQuad({-half, 0, -half}, {half, 0, -half},
                {half, 0, half}, {-half, 0, half}, MAT_SOLID, {0, 1, 0});
    syn.AddQuad({-half, 10, -half}, {half, 10, -half},
                {half, 10, half}, {-half, 10, half}, MAT_SOLID, {0, -1, 0});
    syn.AddQuad({-half, 0, 5}, {half, 0, 5},
                {half, 12, 5}, {-half, 12, 5}, MAT_SOLID, {0, 0, 1});
    syn.Finalize();

    Room room;
    WireRoom(room, syn);
    MoveService mover;
    PlayerState state;
    state.position = {0.0f, mover.Config().half_height, 7.0f}; // feet on floor
    state.velocity = {0.0f, 20.0f, -30.0f};

    const bool wall_pop = mover.Popout(state, room, true);
    const float feet_y = state.position.y - mover.Config().half_height;
    if (!wall_pop || !state.wall_contact || state.ceiling_contact ||
        std::fabs(feet_y) > 0.01f || std::fabs(state.position.z - 8.0f) > 0.02f ||
        std::fabs(state.velocity.y - 20.0f) > 0.02f ||
        std::fabs(state.velocity.z) > 0.02f) {
        printf("FAIL (mixed popout): wall=%u wall_contact=%u ceiling=%u feet=%.3f "
               "z=%.3f v=(%.3f,%.3f,%.3f)\n",
               wall_pop ? 1u : 0u, state.wall_contact ? 1u : 0u,
               state.ceiling_contact ? 1u : 0u, feet_y, state.position.z,
               state.velocity.x, state.velocity.y, state.velocity.z);
        return 1;
    }
    printf("  (mixed popout) ground wins ceiling branch; independent wall still resolves\n");
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
        TickMove(mover, state, room, amount, true);
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

int RunObliqueWallImpactMatchesOgProjection() {
    // Player.Popout (Player.cs:709-714) removes the inward component along the
    // face normal, while positional repair uses radial WallHit.Pushout. Probe
    // just beyond a finite wall edge so these vectors are not interchangeable.
    SyntheticMesh syn;
    syn.Reset();
    constexpr Vec3 normal{0.6f, 0.0f, 0.8f};
    syn.AddQuad({-40, -20, 30}, {40, -20, -30},
                {40, 30, -30}, {-40, 30, 30}, MAT_SOLID, normal);
    syn.Finalize();

    Room room;
    WireRoom(room, syn);
    MoveService mover;
    const float half_height = mover.Config().half_height;
    constexpr Vec3 tangent{0.8f, 0.0f, -0.6f};
    PlayerState state;
    state.position = {normal.x * 2.0f + tangent.x * 51.5f, half_height,
                      normal.z * 2.0f + tangent.z * 51.5f};
    // -50 into the face, +30 along it, and -20 vertically. Source response
    // preserves both tangent components and removes only the inward normal.
    state.velocity = {-6.0f, -20.0f, -58.0f};

    const Vec3 waist = {state.position.x,
                        state.position.y - half_height + 3.0f,
                        state.position.z};
    const WallHit hit = QueryWallPushout(room, waist, mover.Config().wall_radius);
    const float radial_face_dot = hit.normal.x * hit.plane_normal.x +
                                  hit.normal.z * hit.plane_normal.z;
    if (!hit.hit || std::fabs(hit.plane_normal.x - normal.x) > 0.001f ||
        std::fabs(hit.plane_normal.z - normal.z) > 0.001f ||
        radial_face_dot > 0.95f) {
        printf("FAIL (oblique wall): source face not selected: hit=%u n=(%.3f,%.3f,%.3f)\n",
               hit.hit ? 1u : 0u, hit.plane_normal.x, hit.plane_normal.y,
               hit.plane_normal.z);
        return 1;
    }

    const Vec3 before_position = state.position;
    mover.Popout(state, room, true);
    const Vec3 expected_velocity = {24.0f, -20.0f, -18.0f};
    const Vec3 actual_push = {state.position.x - before_position.x,
                              state.position.y - before_position.y,
                              state.position.z - before_position.z};
    const Vec3 expected_push = {hit.normal.x * hit.pushout,
                                hit.normal.y * hit.pushout,
                                hit.normal.z * hit.pushout};
    if (!state.wall_contact ||
        std::fabs(actual_push.x - expected_push.x) > 0.01f ||
        std::fabs(actual_push.y - expected_push.y) > 0.01f ||
        std::fabs(actual_push.z - expected_push.z) > 0.01f ||
        std::fabs(state.velocity.x - expected_velocity.x) > 0.02f ||
        std::fabs(state.velocity.y - expected_velocity.y) > 0.02f ||
        std::fabs(state.velocity.z - expected_velocity.z) > 0.02f) {
        printf("FAIL (oblique wall): push=(%.3f,%.3f,%.3f) v=(%.3f,%.3f,%.3f)\n",
               actual_push.x, actual_push.y, actual_push.z,
               state.velocity.x, state.velocity.y, state.velocity.z);
        return 1;
    }
    printf("  (oblique wall edge) OG separates radial pushout from face-normal velocity projection\n");
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
        TickMove(mover, state, room, amount, true);
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
        TickMove(mover, state, room, amount, true);
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

int RunWallContactClearsAfterLeaving() {
    TallWallNoFloorGeometry geo;
    geo.Build();
    Room room;
    WireRoom(room, geo.syn);
    MoveService mover;

    PlayerState state;
    state.position = {0.0f, 10.0f, 7.5f};  // within radius 3 of wall at z=5
    TickMove(mover, state, room, {0.0f, 0.0f, 0.0f}, false);
    if (!state.wall_contact) {
        printf("FAIL (f): expected wall contact at rest\n");
        return 1;
    }

    TickMove(mover, state, room, {0.0f, 0.0f, 2.0f}, false);  // move away
    if (state.wall_contact) {
        printf("FAIL (f): wall contact stayed latched after leaving wall\n");
        return 1;
    }
    printf("  (f) wall contact clears after leaving OK\n");
    return 0;
}

int RunPlayerModelWallClearance() {
    MotorWallRestGeometry geo;
    geo.Build();
    Room room;
    WireRoom(room, geo.syn);

    PlayerController controller;
    const MoveService& mover = controller.Mover();
    const MoveServiceConfig& config = mover.Config();
    if (std::fabs(config.wall_radius - 3.0f) > 0.01f ||
        std::fabs(config.half_height - 11.25f) > 0.01f ||
        config.climb_wall_radius != 3.0f) {
        printf("FAIL (g): physics shell radius=%.2f half-height=%.2f climb=%.2f; expect 3, 11.25, 3\n",
               config.wall_radius, config.half_height, config.climb_wall_radius);
        return 1;
    }

    PlayerState walking;
    walking.position = {0.0f, config.half_height, 20.0f};
    walking.velocity = {0.0f, 0.0f, -64.0f};
    for (int i = 0; i < 60; ++i) {
        TickMove(mover, walking, room,
            {0.0f, 0.0f, walking.velocity.z * kDt}, true);
    }
    const float walk_clearance = walking.position.z - 5.0f;
    const Vec3 wall_normal = {0.0f, 0.0f, 1.0f};
    const float walk_model_offset = PlayerModelHorizontalOffset(
        wall_normal, walk_clearance, kMadelineModelRadius,
        kMadelineModelHeight, kMadelineModelWallGap);
    const float walk_model_gap = PlayerModelMinimumPlaneGap(
        wall_normal, walk_clearance, kMadelineModelRadius,
        kMadelineModelHeight, walk_model_offset);
    if (std::fabs(walk_clearance - 3.0f) > 0.25f ||
        walk_model_gap < kMadelineModelWallGap - 0.01f) {
        printf("FAIL (g): physics gap %.2f model gap %.2f; expect physics 3 and model >= %.2f\n",
               walk_clearance, walk_model_gap, kMadelineModelWallGap);
        return 1;
    }

    PlayerState climbing;
    climbing.position = {0.0f, config.half_height, 8.0f};  // OG climb anchor: 3 from z=5
    TickMove(mover, climbing, room, {0.0f, 0.0f, -1.0f}, false);
    const float climb_clearance = climbing.position.z - 5.0f;
    const float climb_model_offset = PlayerModelHorizontalOffset(
        wall_normal, climb_clearance, kMadelineModelRadius,
        kMadelineModelHeight, kMadelineModelWallGap);
    const float climb_model_gap = PlayerModelMinimumPlaneGap(
        wall_normal, climb_clearance, kMadelineModelRadius,
        kMadelineModelHeight, climb_model_offset);
    if (std::fabs(climb_clearance - config.climb_wall_radius) > 0.25f ||
        climb_model_gap < kMadelineModelWallGap - 0.01f) {
        printf("FAIL (g): climb gap %.2f model gap %.2f; expect physics 3 and model >= %.2f\n",
               climb_clearance, climb_model_gap, kMadelineModelWallGap);
        return 1;
    }
    printf("  (g) physics/model wall clearance OK: walk %.2f/%.2f climb %.2f/%.2f\n",
           walk_clearance, walk_model_gap, climb_clearance, climb_model_gap);
    return 0;
}

}  // namespace

int main() {
    int failures = 0;
    failures += RunAntiTunnelDash();
    failures += RunStepDownSnap();
    failures += RunDeathContactAccumulatesAcrossSweep();
    failures += RunClimbCancelsGroundSnapWhileSustaining();
    failures += RunControllerStepDownGroundedState(MAT_SOLID);
    failures += RunControllerStepDownGroundedState(MAT_SOLID | MAT_DEATH);
    failures += RunKneeHighLipStepUp();
    failures += RunLipStepRejectsUnsafeRise();
    failures += RunControllerWalksKneeHighLip();
    failures += RunSlopeGround();
    failures += RunCeilingPopoutUsesFeetAnchor();
    failures += RunPopoutMixedGroundCeilingWallOrder();
    failures += RunWallPopoutWaistHead();
    failures += RunObliqueWallImpactMatchesOgProjection();
    failures += RunWallRestEquilibrium();
    failures += RunWallContactClearsAfterLeaving();
    failures += RunPlayerModelWallClearance();

    if (failures == 0) {
        printf("move_service_smoke PASS\n");
        return 0;
    }
    printf("move_service_smoke FAILED (%d fixture(s))\n", failures);
    return 1;
}
