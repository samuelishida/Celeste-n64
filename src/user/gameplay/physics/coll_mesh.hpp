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
    uint16_t version;               // 1 or 2
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
    uint32_t solid_grid_count;      // v2: records immediately follow surface links
};

struct CollVertex   { int16_t pos[3]; };       // 6 B

struct CollTriangle {                           // 12 B
    uint16_t i0, i1, i2;
    uint16_t material;    // bit0=solid bit1=oneway bit2=death bit3=climbable bit4=ice
    uint16_t face_id;     // == array index
    uint16_t source_face_id; // 1-based authored polygon group; 0 = legacy/no provenance
};

struct CollBvhNode {                            // 16 B
    int16_t  aabb_min[3];
    int16_t  aabb_max[3];
    uint16_t left_or_first;   // internal: right-child skip; leaf: first triangle
    uint16_t count_or_zero;   // 0 = internal; > 0 = leaf count
};

struct CollSurfaceLink { uint16_t face_id; uint16_t owner_id; };  // 4 B

struct CollSolidGridRecord {              // 24 B, CMSH v2 only
    uint16_t solid_id;                     // OG static SolidGrid insertion ordinal
    uint16_t first_source_face_id;         // inclusive, source-face ids are ordered by OG face order
    uint16_t last_source_face_id;          // inclusive
    uint16_t reserved;
    float min_x, min_y, max_x, max_y;      // OG query plane coordinates (port X, -port Z)
};

static_assert(sizeof(CollHeader) == 72, "CMSH header layout changed");
static_assert(sizeof(CollTriangle) == 12, "CMSH triangle layout changed");
static_assert(sizeof(CollSolidGridRecord) == 24, "CMSH v2 record layout changed");

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
    const CollSolidGridRecord* solid_grid = nullptr;
    // Pre-dequantized float vertices — allocated at load, freed by FreeCollMesh.
    // world_verts[i] == DequantVert(mesh, vertices[i]) for all i.
    Vec3* world_verts = nullptr;
    // backing allocation — owned; freed by FreeCollMesh
    void* _buffer = nullptr;

    // memory-opt Inc 1: live byte estimate (backing file buffer + dequantized
    // world vertices). 0 for a null/partial mesh.
    size_t UsedBytes() const {
        size_t bytes = 0;
        if (_buffer != nullptr && header != nullptr) {
            const size_t after_links = header->surface_link_offset +
                static_cast<size_t>(header->surface_link_count) * sizeof(CollSurfaceLink);
            bytes += header->version >= 2
                ? ((after_links + 7u) & ~static_cast<size_t>(7u))
                : after_links;
            bytes += static_cast<size_t>(header->solid_grid_count) *
                     sizeof(CollSolidGridRecord);
        }
        if (world_verts != nullptr && header != nullptr) {
            bytes += static_cast<size_t>(header->vertex_count) * sizeof(Vec3);
        }
        return bytes;
    }
};

// Load a .colmesh from a filesystem path (host tests) or rom:/ path (N64).
// Returns nullptr on bad magic/version or alloc failure.
// Caller must pair with FreeCollMesh.
CollMesh* LoadCollMesh(const char* path);
// memory-opt Inc 2: load from an in-RAM buffer (the asset path). Copies the
// data (the parse byte-swaps in place); the caller may free `data` after.
// Returns nullptr on bad magic/version/alloc failure; caller pairs with
// FreeCollMesh.
CollMesh* LoadCollMeshFromBuffer(const uint8_t* data, size_t size);
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

using TriangleFilter = bool (*)(const CollMesh& mesh, int triangle_index,
                                void* user_data);

// Raycast from 'origin' in direction 'dir' (NOT normalized; t is in [0, max_t]).
// Returns nearest hit with t <= max_t; exact ties choose lowest stable face_id.
// 'out_nodes_touched' is optional; when non-null it receives the number of BVH
// nodes visited during traversal.
RayHit RaycastMesh(const CollMesh& mesh,
                   Vec3 origin, Vec3 dir, float max_t,
                   BackfaceCull cull = BackfaceCull::Ignore,
                   uint32_t* out_nodes_touched = nullptr);
RayHit RaycastMeshFiltered(const CollMesh& mesh,
                           Vec3 origin, Vec3 dir, float max_t,
                           BackfaceCull cull, TriangleFilter filter,
                           void* user_data,
                           uint32_t* out_nodes_touched = nullptr);

// Axis-aligned vertical raycast fast path for floor/ceiling queries.
// Direction must be exactly +Y or -Y.  Avoids the generic slab setup and
// traverses fewer BVH nodes because the X/Z origin is fixed.
RayHit RaycastMeshVertical(const CollMesh& mesh,
                           Vec3 origin, float dir_y, float max_t,
                           BackfaceCull cull = BackfaceCull::Ignore,
                           uint32_t* out_nodes_touched = nullptr);

// Sweep sphere of 'radius' from 'origin' along 'dir' (dir is normalised, distance <= max_dist).
// Returns nearest hit; exact ties choose lowest stable face_id.
// 'out_nodes_touched' is optional; when non-null it receives the number of BVH
// nodes visited during traversal.
SweepSphereHit SweepSphereMesh(const CollMesh& mesh,
                                const Vec3& origin, const Vec3& dir,
                                float radius, float max_dist,
                                uint32_t* out_nodes_touched = nullptr,
                                TriangleFilter filter = nullptr,
                                void* user_data = nullptr);

// Returns face_ids of all triangles whose AABB overlaps 'query'.
// Writes at most 'max_out' face_ids to 'out_face_ids'. Returns count written.
// 'out_nodes_touched' is optional; when non-null it receives the number of BVH
// nodes visited during traversal.
int OverlapAabbMesh(const CollMesh& mesh, const AABB& query,
                    int* out_face_ids, int max_out,
                    uint32_t* out_nodes_touched = nullptr);

// Visit every triangle in every BVH leaf overlapping query without a fixed
// candidate buffer. Return visited count; visitor may return false to stop.
int VisitOverlappingTriangles(const CollMesh& mesh, const AABB& query,
                              bool (*visitor)(int face_id, void* user_data),
                              void* user_data,
                              uint32_t* out_nodes_touched = nullptr);

// Binary search over sorted SurfaceLink[].
// Returns INVALID_OWNER if face_id is not a moving surface.
uint16_t SurfaceOwnerOf(const CollMesh& mesh, int face_id);

// CMSH v2 lookup: returns OG static-solid metadata owning this triangle's
// authored face, or nullptr for legacy/unmodeled actor geometry.
const CollSolidGridRecord* SolidGridRecordOf(const CollMesh& mesh, int face_id);
// Find lowest stable triangle id belonging to an authored face. Returns -1
// when provenance is absent. Fan triangles remain individually addressable.
inline int SourceFaceTriangleOf(const CollMesh& mesh, uint16_t source_face_id) {
    if (!mesh.header || !mesh.triangles || source_face_id == 0) return -1;
    int best = -1;
    for (uint32_t i = 0; i < mesh.header->triangle_count; ++i) {
        const CollTriangle& triangle = mesh.triangles[i];
        if (triangle.source_face_id != source_face_id) continue;
        const int stable_id = static_cast<int>(triangle.face_id);
        if (best < 0 || stable_id < best) best = stable_id;
    }
    return best;
}
inline uint16_t SourceFaceMaterialOf(const CollMesh& mesh,
                                    uint16_t source_face_id) {
    const int triangle_id = SourceFaceTriangleOf(mesh, source_face_id);
    return triangle_id >= 0 &&
        static_cast<uint32_t>(triangle_id) < mesh.header->triangle_count
        ? mesh.triangles[triangle_id].material : 0;
}

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
