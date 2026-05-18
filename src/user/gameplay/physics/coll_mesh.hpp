#pragma once

#include <cstdint>
#include <cstddef>

#include "gameplay/math_types.hpp"
#include "gameplay/physics/geom.hpp"

namespace madeline_cube::physics {

// ---------------------------------------------------------------------------
// On-disk types (big-endian; matches colmesh_format.md)
// ---------------------------------------------------------------------------

struct CollHeader {
    char     magic[4];              // "CMSH"
    uint16_t version;               // 1
    uint16_t flags;                 // bit0 = has_bvh
    int16_t  aabb_min[3];           // quantized mesh AABB
    int16_t  aabb_max[3];
    float    quant_scale;
    float    quant_origin[3];
    uint32_t vertex_count;          // ≤ 65535
    uint32_t triangle_count;        // ≤ 32767
    uint32_t bvh_node_count;        // ≤ 65535
    uint32_t surface_link_count;
    uint32_t vertex_offset;         // byte offset from file start; 8-byte aligned
    uint32_t triangle_offset;
    uint32_t bvh_offset;
    uint32_t surface_link_offset;
};

struct CollVertex   { int16_t pos[3]; };       // 6 B

struct CollTriangle {                           // 12 B
    uint16_t i0, i1, i2;
    uint16_t material;    // bit0=solid bit1=oneway bit2=death bit3=climbable bit4=ice
    uint16_t face_id;     // == array index
    uint16_t pad;         // always 0
};

struct CollBvhNode {                            // 16 B
    int16_t  aabb_min[3];
    int16_t  aabb_max[3];
    uint16_t left_or_first;   // internal: right-child skip; leaf: first triangle
    uint16_t count_or_zero;   // 0 = internal; > 0 = leaf count
};

struct CollSurfaceLink { uint16_t face_id; uint16_t owner_id; };  // 4 B

static constexpr uint16_t INVALID_OWNER = 0xFFFF;

// Material flag bits
static constexpr uint16_t MAT_SOLID     = 0x0001;
static constexpr uint16_t MAT_ONEWAY    = 0x0002;
static constexpr uint16_t MAT_DEATH     = 0x0004;
static constexpr uint16_t MAT_CLIMBABLE = 0x0008;
static constexpr uint16_t MAT_ICE       = 0x0010;

// ---------------------------------------------------------------------------
// Runtime handle — pointers into a loaded flat buffer
// ---------------------------------------------------------------------------

struct CollMesh {
    const CollHeader*      header;
    const CollVertex*      vertices;
    const CollTriangle*    triangles;
    const CollBvhNode*     bvh_nodes;
    const CollSurfaceLink* surface_links;
    // Pre-dequantized float vertices — allocated at load, freed by FreeCollMesh.
    // world_verts[i] == DequantVert(mesh, vertices[i]) for all i.
    Vec3* world_verts = nullptr;
    // backing allocation — owned; freed by FreeCollMesh
    void* _buffer = nullptr;
};

// Load a .colmesh from a filesystem path (host tests) or rom:/ path (N64).
// Returns nullptr on bad magic/version or alloc failure.
// Caller must pair with FreeCollMesh.
CollMesh* LoadCollMesh(const char* path);
void      FreeCollMesh(CollMesh* mesh);

// ---------------------------------------------------------------------------
// Query API (read-only, reentrant)
// ---------------------------------------------------------------------------

struct RayHit {
    bool     hit     = false;
    float    t       = 0.0f;      // distance along direction (direction is UNnormalized — t is fraction, not world distance)
    Vec3     point;
    Vec3     normal;              // normalised face normal pointing toward ray origin side
    int      face_id = -1;
    uint16_t material = 0;
};

struct SweepSphereHit {
    bool     hit     = false;
    float    t       = 0.0f;      // fraction along sweep [0,1]
    Vec3     point;               // contact point on triangle surface
    Vec3     normal;              // normal pointing away from surface, toward sphere centre
    int      face_id = -1;
    uint16_t material = 0;
};

// Raycast from 'origin' in direction 'dir' (NOT normalized; t is in [0, max_t]).
// Returns nearest hit with t <= max_t.
RayHit RaycastMesh(const CollMesh& mesh,
                   const Vec3& origin, const Vec3& dir, float max_t,
                   BackfaceCull cull = BackfaceCull::Ignore);

// Sweep sphere of 'radius' from 'origin' along 'dir' (dir is normalised, distance <= max_dist).
// Returns nearest hit.
SweepSphereHit SweepSphereMesh(const CollMesh& mesh,
                                const Vec3& origin, const Vec3& dir,
                                float radius, float max_dist);

// Returns face_ids of all triangles whose AABB overlaps 'query'.
// Writes at most 'max_out' face_ids to 'out_face_ids'. Returns count written.
int OverlapAabbMesh(const CollMesh& mesh, const AABB& query,
                    int* out_face_ids, int max_out);

// Binary search over sorted SurfaceLink[].
// Returns INVALID_OWNER if face_id is not a moving surface.
uint16_t SurfaceOwnerOf(const CollMesh& mesh, int face_id);

// ---------------------------------------------------------------------------
// Internal: dequantize a quantized int16 position to world space
// ---------------------------------------------------------------------------

inline Vec3 DequantVert(const CollMesh& m, const CollVertex& v) {
    const float s = m.header->quant_scale;
    return {
        m.header->quant_origin[0] + v.pos[0] * s,
        m.header->quant_origin[1] + v.pos[1] * s,
        m.header->quant_origin[2] + v.pos[2] * s,
    };
}

inline AABB DequantAabb(const CollMesh& m, const int16_t mn[3], const int16_t mx[3]) {
    const float s = m.header->quant_scale;
    const float* o = m.header->quant_origin;
    return {
        { o[0] + mn[0] * s, o[1] + mn[1] * s, o[2] + mn[2] * s },
        { o[0] + mx[0] * s, o[1] + mx[1] * s, o[2] + mx[2] * s }
    };
}

}  // namespace madeline_cube::physics
