// Host test for one-way platforms (§): MAT_ONEWAY faces act as floors when
// landing from above but are pass-through from below.
//
// Build:
//   g++ -std=c++17 -Isrc/user tests/feel_spec/oneway_smoke.cpp \
//       src/user/gameplay/player/player_motor.cpp \
//       src/user/gameplay/world/world.cpp \
//       src/user/gameplay/world/room_data.cpp \
//       src/user/gameplay/physics/coll_mesh.cpp \
//       src/user/gameplay/physics/geom.cpp \
//       src/user/gameplay/runtime/math.cpp -o /tmp/oneway_smoke && /tmp/oneway_smoke
#include <cassert>
#include <cmath>
#include <cstring>

#include "gameplay/physics/coll_mesh.hpp"
#include "gameplay/player/player_motor.hpp"
#include "gameplay/player/player_state.hpp"
#include "gameplay/world/world.hpp"

using namespace madeline_cube;
using namespace madeline_cube::physics;

namespace {
constexpr float kDt = 1.0f / 60.0f;

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
    // --- Landing from above on a one-way platform -> grounded ---
    {
        CollMesh* mesh = BuildOneTriangleMesh(MAT_ONEWAY);
        Room room;
        room.coll_mesh = mesh;
        PlayerMotor motor;
        PlayerState state;
        state.position = {0.0f, 20.0f, 0.0f};
        state.velocity = {0.0f, -10.0f, 0.0f};
        MotorInput mi;
        mi.requested_velocity = state.velocity;
        bool grounded = false;
        for (int i = 0; i < 120; ++i) {
            MotorResult r = motor.Step(state, room, mi, kDt);
            if (r.grounded) { grounded = true; break; }
        }
        assert(grounded);
        assert(state.grounded);
        FreeMesh(mesh);
    }

    // --- Passing through from below (moving up) -> NOT blocked ---
    {
        CollMesh* mesh = BuildOneTriangleMesh(MAT_ONEWAY);
        Room room;
        room.coll_mesh = mesh;
        PlayerMotor motor;
        PlayerState state;
        state.position = {0.0f, -20.0f, 0.0f};
        state.velocity = {0.0f, 10.0f, 0.0f};
        MotorInput mi;
        mi.requested_velocity = state.velocity;
        // The player must pass through the platform (y crosses 0) without being
        // stopped by a ceiling collision. Once above, gravity may re-ground them,
        // which is correct Celeste behavior — the key is they were not blocked.
        bool passed_through = false;
        for (int i = 0; i < 200; ++i) {
            MotorResult r = motor.Step(state, room, mi, kDt);
            if (state.position.y > 0.0f) { passed_through = true; break; }
        }
        assert(passed_through);
        FreeMesh(mesh);
    }

    // --- Solid platform (MAT_SOLID) still blocks from below ---
    {
        CollMesh* mesh = BuildOneTriangleMesh(MAT_SOLID);
        Room room;
        room.coll_mesh = mesh;
        PlayerMotor motor;
        PlayerState state;
        state.position = {0.0f, -20.0f, 0.0f};
        state.velocity = {0.0f, 10.0f, 0.0f};
        MotorInput mi;
        mi.requested_velocity = state.velocity;
        for (int i = 0; i < 120; ++i) {
            MotorResult r = motor.Step(state, room, mi, kDt);
            if (r.grounded) break;
        }
        // Solid blocks: player should be stopped below the platform.
        assert(state.position.y < 0.0f);
        FreeMesh(mesh);
    }

    return 0;
}
