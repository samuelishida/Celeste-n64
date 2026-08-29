// Host test for one-way platforms (§): MAT_ONEWAY faces act as floors when
// landing from above but are pass-through from below.
//
// og-mover-overhaul Inc 2: the motor is deleted; this validates the one-way
// gates in MoveService (ray GroundCheck + CeilingCheck) directly.
//
// Build:
//   g++ -std=c++17 -Isrc/user tests/feel_spec/oneway_smoke.cpp \
//       src/user/gameplay/player/player_movement.cpp \
//       src/user/gameplay/world/world.cpp \
//       src/user/gameplay/physics/coll_mesh.cpp \
//       src/user/gameplay/physics/geom.cpp \
//       src/user/gameplay/runtime/math.cpp -o /tmp/oneway_smoke && /tmp/oneway_smoke
#include <cassert>
#include <cmath>
#include <cstring>

#include "gameplay/physics/coll_mesh.hpp"
#include "gameplay/player/player_movement.hpp"
#include "gameplay/player/player_state.hpp"
#include "gameplay/world/world.hpp"

using namespace madeline_cube;
using namespace madeline_cube::physics;

namespace {
constexpr float kDt = 1.0f / 60.0f;
constexpr float kGravity = 600.0f;

// Build a CollMesh with a single flat horizontal triangle at y=0 spanning
// x,z in [-50,50], marked with the given material.
CollMesh* BuildOneTriangleMesh(uint16_t material) {
    CollMesh* mesh = new CollMesh();
    CollHeader* hdr = new CollHeader();
    mesh->header = hdr;
    CollHeader& h = *hdr;
    std::memset(&h, 0, sizeof(h));
    std::memcpy(h.magic, "CMSH", 4);
    h.version = 1;
    h.quant_scale = 1.0f;
    h.quant_origin[0] = 0.0f;
    h.quant_origin[1] = 0.0f;
    h.quant_origin[2] = 0.0f;
    h.vertex_count = 3;
    h.triangle_count = 1;
    h.bvh_node_count = 1;

    CollVertex* verts = new CollVertex[3];
    verts[0].pos[0] = -50; verts[0].pos[1] = 0; verts[0].pos[2] = -50;
    verts[1].pos[0] =  50; verts[1].pos[1] = 0; verts[1].pos[2] = -50;
    verts[2].pos[0] =  0;  verts[2].pos[1] = 0; verts[2].pos[2] =  50;

    CollTriangle* tris = new CollTriangle[1];
    tris[0].i0 = 0; tris[0].i1 = 1; tris[0].i2 = 2;
    tris[0].material = material;
    tris[0].face_id = 0;
    tris[0].pad = 0;

    CollBvhNode* nodes = new CollBvhNode[1];
    nodes[0].aabb_min[0] = -50; nodes[0].aabb_min[1] = 0; nodes[0].aabb_min[2] = -50;
    nodes[0].aabb_max[0] =  50; nodes[0].aabb_max[1] = 0; nodes[0].aabb_max[2] =  50;
    nodes[0].left_or_first = 0;
    nodes[0].count_or_zero = 1;

    mesh->vertices = verts;
    mesh->triangles = tris;
    mesh->bvh_nodes = nodes;
    mesh->surface_links = nullptr;
    mesh->world_verts = new Vec3[3];
    for (int i = 0; i < 3; ++i) mesh->world_verts[i] = DequantVert(*mesh, verts[i]);
    return mesh;
}

void FreeMesh(CollMesh* mesh) {
    if (!mesh) return;
    delete[] mesh->world_verts;
    delete[] mesh->bvh_nodes;
    delete[] mesh->triangles;
    delete[] mesh->vertices;
    delete mesh->header;
    delete mesh;
}
}

int main() {
    // --- Landing from above on a one-way platform -> GroundCheck grounds.
    //     Feet exactly on the surface (center = half_height above y=0): the
    //     ray from feet+5 hits the one-way; the feet-at-or-above gate accepts. ---
    {
        CollMesh* mesh = BuildOneTriangleMesh(MAT_ONEWAY);
        Room room;
        room.coll_mesh = mesh;
        MoveService mover;
        PlayerState state;
        state.position = {0.0f, 10.0f, 0.0f};  // feet at y=0, ON the platform
        Vec3 pushout{};
        const bool grounded = mover.GroundCheck(state, room, &pushout, nullptr);
        assert(grounded);
        assert(pushout.y >= 0.0f);           // feet stay at or above the surface
        FreeMesh(mesh);
    }

    // --- From below (feet strictly below the one-way surface) -> NOT a floor.
    //     CeilingCheck must NOT block a one-way approached from below. ---
    {
        CollMesh* mesh = BuildOneTriangleMesh(MAT_ONEWAY);
        Room room;
        room.coll_mesh = mesh;
        MoveService mover;
        PlayerState state;
        // Feet just below the platform surface (center y=8 -> feet y=-2, within
        // the ceiling ray reach of feet+1..feet+12).
        state.position = {0.0f, 8.0f, 0.0f};
        // GroundCheck from below misses (no floor underneath).
        assert(!mover.GroundCheck(state, room, nullptr, nullptr));
        // CeilingCheck sees the one-way above but the pass-through-from-below
        // gate rejects it -> no ceiling contact (NOT blocked).
        assert(!mover.CeilingCheck(state, room, nullptr));
        FreeMesh(mesh);
    }

    // --- Solid platform still blocks from below via CeilingCheck. ---
    {
        CollMesh* mesh = BuildOneTriangleMesh(MAT_SOLID);
        Room room;
        room.coll_mesh = mesh;
        MoveService mover;
        PlayerState state;
        state.position = {0.0f, 8.0f, 0.0f};  // feet at y=-2, below the platform
        bool blocked = mover.CeilingCheck(state, room, nullptr);
        assert(blocked);
        FreeMesh(mesh);
    }

    return 0;
}
