#include "coll_mesh.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cfloat>
#include <algorithm>

namespace madeline_cube::physics {

// ---------------------------------------------------------------------------
// Big-endian read helpers (N64 native; also correct on big-endian host)
// On little-endian x86 host these byteswap so tests work.
// ---------------------------------------------------------------------------

namespace {

static inline uint16_t BE16(uint16_t x) {
#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
    return (uint16_t)((x >> 8) | (x << 8));
#else
    return x;
#endif
}

static inline int16_t BE16s(int16_t x) {
    return (int16_t)BE16((uint16_t)x);
}

static inline uint32_t BE32(uint32_t x) {
#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
    return ((x & 0xFF000000u) >> 24) | ((x & 0x00FF0000u) >> 8)
         | ((x & 0x0000FF00u) << 8)  | ((x & 0x000000FFu) << 24);
#else
    return x;
#endif
}

static inline float BEf(float x) {
    uint32_t bits;
    memcpy(&bits, &x, 4);
    bits = BE32(bits);
    memcpy(&x, &bits, 4);
    return x;
}

// Byteswap a CollHeader loaded from disk into host byte order.
static void SwapHeader(CollHeader* h) {
    h->version              = BE16(h->version);
    h->flags                = BE16(h->flags);
    for (int i = 0; i < 3; ++i) h->aabb_min[i]    = BE16s(h->aabb_min[i]);
    for (int i = 0; i < 3; ++i) h->aabb_max[i]    = BE16s(h->aabb_max[i]);
    h->quant_scale          = BEf(h->quant_scale);
    for (int i = 0; i < 3; ++i) h->quant_origin[i] = BEf(h->quant_origin[i]);
    h->vertex_count         = BE32(h->vertex_count);
    h->triangle_count       = BE32(h->triangle_count);
    h->bvh_node_count       = BE32(h->bvh_node_count);
    h->surface_link_count   = BE32(h->surface_link_count);
    h->vertex_offset        = BE32(h->vertex_offset);
    h->triangle_offset      = BE32(h->triangle_offset);
    h->bvh_offset           = BE32(h->bvh_offset);
    h->surface_link_offset  = BE32(h->surface_link_offset);
}

static void SwapVertices(CollVertex* verts, uint32_t count) {
    for (uint32_t i = 0; i < count; ++i)
        for (int j = 0; j < 3; ++j)
            verts[i].pos[j] = BE16s(verts[i].pos[j]);
}

static void SwapTriangles(CollTriangle* tris, uint32_t count) {
    for (uint32_t i = 0; i < count; ++i) {
        tris[i].i0       = BE16(tris[i].i0);
        tris[i].i1       = BE16(tris[i].i1);
        tris[i].i2       = BE16(tris[i].i2);
        tris[i].material = BE16(tris[i].material);
        tris[i].face_id  = BE16(tris[i].face_id);
        // pad stays 0 — no swap needed
    }
}

static void SwapBvhNodes(CollBvhNode* nodes, uint32_t count) {
    for (uint32_t i = 0; i < count; ++i) {
        for (int j = 0; j < 3; ++j) nodes[i].aabb_min[j] = BE16s(nodes[i].aabb_min[j]);
        for (int j = 0; j < 3; ++j) nodes[i].aabb_max[j] = BE16s(nodes[i].aabb_max[j]);
        nodes[i].left_or_first = BE16(nodes[i].left_or_first);
        nodes[i].count_or_zero = BE16(nodes[i].count_or_zero);
    }
}

static void SwapSurfaceLinks(CollSurfaceLink* links, uint32_t count) {
    for (uint32_t i = 0; i < count; ++i) {
        links[i].face_id  = BE16(links[i].face_id);
        links[i].owner_id = BE16(links[i].owner_id);
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// Load / free
// ---------------------------------------------------------------------------

CollMesh* LoadCollMesh(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return nullptr;

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (file_size < (long)sizeof(CollHeader)) { fclose(f); return nullptr; }

    void* buf = malloc((size_t)file_size);
    if (!buf) { fclose(f); return nullptr; }

    if (fread(buf, 1, (size_t)file_size, f) != (size_t)file_size) {
        free(buf); fclose(f); return nullptr;
    }
    fclose(f);

    CollHeader* hdr = static_cast<CollHeader*>(buf);
    if (memcmp(hdr->magic, "CMSH", 4) != 0) { free(buf); return nullptr; }

    SwapHeader(hdr);

    if (hdr->version != 1) { free(buf); return nullptr; }
    if (!(hdr->quant_scale > 0.f))  { free(buf); return nullptr; }
    // BVH leaf count_or_zero and left_or_first are uint16_t; max usable triangles = 32767.
    if (hdr->triangle_count > 32767) { free(buf); return nullptr; }

    // Validate all offset + count * sizeof(element) <= file_size
    {
        auto offset_ok = [&](uint32_t off, uint32_t count, size_t elem_sz) -> bool {
            if (off > (uint32_t)file_size) return false;
            uint64_t end = (uint64_t)off + (uint64_t)count * elem_sz;
            return end <= (uint64_t)file_size;
        };
        if (!offset_ok(hdr->vertex_offset,       hdr->vertex_count,       sizeof(CollVertex)) ||
            !offset_ok(hdr->triangle_offset,     hdr->triangle_count,     sizeof(CollTriangle)) ||
            !offset_ok(hdr->bvh_offset,          hdr->bvh_node_count,     sizeof(CollBvhNode)) ||
            !offset_ok(hdr->surface_link_offset, hdr->surface_link_count, sizeof(CollSurfaceLink))) {
            free(buf);
            return nullptr;
        }
    }

    char* base = static_cast<char*>(buf);
    SwapVertices(  reinterpret_cast<CollVertex*>  (base + hdr->vertex_offset),    hdr->vertex_count);
    SwapTriangles( reinterpret_cast<CollTriangle*>(base + hdr->triangle_offset),  hdr->triangle_count);

    // Validate triangle vertex indices are in bounds
    {
        const CollTriangle* tris = reinterpret_cast<const CollTriangle*>(base + hdr->triangle_offset);
        for (uint32_t i = 0; i < hdr->triangle_count; ++i) {
            if (tris[i].i0 >= hdr->vertex_count ||
                tris[i].i1 >= hdr->vertex_count ||
                tris[i].i2 >= hdr->vertex_count) {
                free(buf);
                return nullptr;
            }
        }
    }

    SwapBvhNodes(  reinterpret_cast<CollBvhNode*> (base + hdr->bvh_offset),       hdr->bvh_node_count);
    SwapSurfaceLinks(reinterpret_cast<CollSurfaceLink*>(base + hdr->surface_link_offset),
                     hdr->surface_link_count);

    // Validate BVH leaf triangle ranges after byte-swapping so the host loader
    // sees the same values the runtime will query.
    {
        const CollBvhNode* nodes = reinterpret_cast<const CollBvhNode*>(base + hdr->bvh_offset);
        for (uint32_t i = 0; i < hdr->bvh_node_count; ++i) {
            if (nodes[i].count_or_zero > 0) {
                uint32_t first = nodes[i].left_or_first;
                uint32_t last  = first + nodes[i].count_or_zero;
                if (first >= hdr->triangle_count || last > hdr->triangle_count) {
                    free(buf);
                    return nullptr;
                }
            }
        }
    }

    CollMesh* mesh = new CollMesh();
    mesh->header       = hdr;
    mesh->vertices     = reinterpret_cast<const CollVertex*>  (base + hdr->vertex_offset);
    mesh->triangles    = reinterpret_cast<const CollTriangle*>(base + hdr->triangle_offset);
    mesh->bvh_nodes    = reinterpret_cast<const CollBvhNode*> (base + hdr->bvh_offset);
    mesh->surface_links= reinterpret_cast<const CollSurfaceLink*>(base + hdr->surface_link_offset);
    mesh->_buffer      = buf;

    // Pre-dequantize all vertices so hot-path queries avoid float multiply per vertex.
    mesh->world_verts = new Vec3[hdr->vertex_count];
    for (uint32_t i = 0; i < hdr->vertex_count; ++i) {
        mesh->world_verts[i] = DequantVert(*mesh, mesh->vertices[i]);
    }

    return mesh;
}

void FreeCollMesh(CollMesh* mesh) {
    if (!mesh) return;
    delete[] mesh->world_verts;
    free(mesh->_buffer);
    delete mesh;
}

// ---------------------------------------------------------------------------
// BVH traversal
// ---------------------------------------------------------------------------

namespace {

// Ray vs AABB slab test (world-space, using precomputed inv_dir).
inline bool RayAabb(const Vec3& origin, const float inv_dir[3],
                    const AABB& box, float t_max) {
    float t_enter = 0.f, t_exit = t_max;
    for (int i = 0; i < 3; ++i) {
        float bmin = (&box.min.x)[i];
        float bmax = (&box.max.x)[i];
        float t1 = (bmin - (&origin.x)[i]) * inv_dir[i];
        float t2 = (bmax - (&origin.x)[i]) * inv_dir[i];
        if (t1 > t2) { float tmp = t1; t1 = t2; t2 = tmp; }
        t_enter = t_enter > t1 ? t_enter : t1;
        t_exit  = t_exit  < t2 ? t_exit  : t2;
        if (t_enter > t_exit) return false;
    }
    return true;
}

// AABB overlap (expanded for sphere sweep by 'radius').
inline bool AabbInflated(const AABB& box, const Vec3& qmin, const Vec3& qmax) {
    return box.min.x <= qmax.x && box.max.x >= qmin.x
        && box.min.y <= qmax.y && box.max.y >= qmin.y
        && box.min.z <= qmax.z && box.max.z >= qmin.z;
}

}  // namespace

// ---------------------------------------------------------------------------
// RaycastMesh
// ---------------------------------------------------------------------------

RayHit RaycastMesh(const CollMesh& mesh,
                   Vec3 origin, Vec3 dir, float max_t,
                   BackfaceCull cull)
{
    if (mesh.header->bvh_node_count == 0) return {};

    float inv_dir[3] = {
        std::fabs(dir.x) > 1e-7f ? 1.f / dir.x : 1e30f,
        std::fabs(dir.y) > 1e-7f ? 1.f / dir.y : 1e30f,
        std::fabs(dir.z) > 1e-7f ? 1.f / dir.z : 1e30f,
    };

    // Segment endpoint p1 = origin + dir * max_t
    Vec3 p1 = { origin.x + dir.x * max_t,
                origin.y + dir.y * max_t,
                origin.z + dir.z * max_t };

    RayHit best;
    // best_world_t is in the same units as RayAabb (world-space distance along
    // dir).  SegmentTriangle returns t ∈ [0,1] (fraction of segment p0→p1
    // whose length is max_t world units), so h.t * max_t gives world distance.
    // Keeping two separate variables avoids the unit-mismatch that would let
    // RayAabb prune valid nodes prematurely.
    float best_world_t = max_t + 1.f;  // world units; used by RayAabb
    float best_t = 2.f;                // segment param [0,1]; used for comparison

    uint16_t stack[64];
    int top = 0;
    stack[top++] = 0;

    while (top > 0) {
        uint16_t idx = stack[--top];
        const CollBvhNode& node = mesh.bvh_nodes[idx];
        AABB aabb = DequantAabb(mesh, node.aabb_min, node.aabb_max);

        if (!RayAabb(origin, inv_dir, aabb, best_world_t)) continue;

        if (node.count_or_zero > 0) {
            // Leaf — test triangles
            int first = node.left_or_first;
            int count = node.count_or_zero;
            for (int ti = first; ti < first + count; ++ti) {
                const CollTriangle& ct = mesh.triangles[ti];
                const Vec3& a = mesh.world_verts[ct.i0];
                const Vec3& b = mesh.world_verts[ct.i1];
                const Vec3& c = mesh.world_verts[ct.i2];
                TriHit h = SegmentTriangle(origin, p1, a, b, c, cull);
                if (h.hit && h.t < best_t) {
                    best_world_t   = h.t * max_t;
                    best_t         = h.t;
                    best.hit       = true;
                    best.t         = h.t;
                    best.point     = h.point;
                    // normalize and ensure normal faces ray origin
                    Vec3 n = h.normal;
                    float len = std::sqrt(n.x*n.x + n.y*n.y + n.z*n.z);
                    if (len > 1e-7f) { n.x /= len; n.y /= len; n.z /= len; }
                    float nd = n.x*dir.x + n.y*dir.y + n.z*dir.z;
                    if (nd > 0.f) { n.x = -n.x; n.y = -n.y; n.z = -n.z; }
                    best.normal    = n;
                    best.face_id   = (int)ct.face_id;
                    best.material  = ct.material;
                }
            }
        } else {
            // Internal — push right then left (left is processed first = LIFO)
            {
                uint16_t right_child = (uint16_t)(idx + node.left_or_first);
                uint16_t left_child  = (uint16_t)(idx + 1);
                if (top + 2 <= 64
                    && right_child < mesh.header->bvh_node_count
                    && left_child  < mesh.header->bvh_node_count) {
                    stack[top++] = right_child;  // right
                    stack[top++] = left_child;   // left
                }
            }
        }
    }
    return best;
}

// ---------------------------------------------------------------------------
// SweepSphereMesh
// ---------------------------------------------------------------------------

SweepSphereHit SweepSphereMesh(const CollMesh& mesh,
                                const Vec3& origin, const Vec3& dir,
                                float radius, float max_dist)
{
    if (mesh.header->bvh_node_count == 0) return {};

    // Sweep AABB for BVH broad-phase
    Vec3 end = { origin.x + dir.x * max_dist,
                 origin.y + dir.y * max_dist,
                 origin.z + dir.z * max_dist };

    auto sweep_min = [&]() {
        return Vec3{ std::fmin(origin.x, end.x) - radius,
                     std::fmin(origin.y, end.y) - radius,
                     std::fmin(origin.z, end.z) - radius };
    };
    auto sweep_max = [&]() {
        return Vec3{ std::fmax(origin.x, end.x) + radius,
                     std::fmax(origin.y, end.y) + radius,
                     std::fmax(origin.z, end.z) + radius };
    };

    Vec3 qmin = sweep_min(), qmax = sweep_max();

    SweepSphereHit best;
    float best_t = max_dist + 1.f;

    uint16_t stack[64];
    int top = 0;
    stack[top++] = 0;

    while (top > 0) {
        uint16_t idx = stack[--top];
        const CollBvhNode& node = mesh.bvh_nodes[idx];
        AABB aabb = DequantAabb(mesh, node.aabb_min, node.aabb_max);

        if (!AabbInflated(aabb, qmin, qmax)) continue;

        if (node.count_or_zero > 0) {
            // Leaf
            int first = node.left_or_first;
            int count = node.count_or_zero;
            for (int ti = first; ti < first + count; ++ti) {
                const CollTriangle& ct = mesh.triangles[ti];
                if (!(ct.material & MAT_SOLID)) continue;   // non-solid = skip

                const Vec3& a = mesh.world_verts[ct.i0];
                const Vec3& b = mesh.world_verts[ct.i1];
                const Vec3& c = mesh.world_verts[ct.i2];

                // If already touching at origin, record t=0 and move on.
                // Simplified sweep: 8 linear samples along the path.
                // Sufficient for motion steps < sphere radius; not an analytic TOI.
                SphereTriHit sh0 = SphereTriangle(origin, radius, a, b, c);
                if (sh0.hit) {
                    best_t         = 0.f;
                    best.hit       = true;
                    best.t         = 0.f;
                    best.point     = sh0.closest;
                    best.normal    = sh0.normal;
                    best.face_id   = (int)ct.face_id;
                    best.material  = ct.material;
                    continue;
                }
                for (int s = 1; s <= 8; ++s) {
                    float ft = (float)s / 8.f * max_dist;
                    if (ft >= best_t) break;
                    Vec3 centre = { origin.x + dir.x * ft,
                                    origin.y + dir.y * ft,
                                    origin.z + dir.z * ft };
                    SphereTriHit sh = SphereTriangle(centre, radius, a, b, c);
                    if (sh.hit && ft < best_t) {
                        best_t         = ft;
                        best.hit       = true;
                        best.t         = ft / max_dist;
                        best.point     = sh.closest;
                        best.normal    = sh.normal;
                        best.face_id   = (int)ct.face_id;
                        best.material  = ct.material;
                        break;
                    }
                }
            }
        } else {
            {
                uint16_t right_child = (uint16_t)(idx + node.left_or_first);
                uint16_t left_child  = (uint16_t)(idx + 1);
                if (top + 2 <= 64
                    && right_child < mesh.header->bvh_node_count
                    && left_child  < mesh.header->bvh_node_count) {
                    stack[top++] = right_child;
                    stack[top++] = left_child;
                }
            }
        }
    }
    return best;
}

// ---------------------------------------------------------------------------
// OverlapAabbMesh
// ---------------------------------------------------------------------------

int OverlapAabbMesh(const CollMesh& mesh, const AABB& query,
                    int* out_face_ids, int max_out)
{
    if (mesh.header->bvh_node_count == 0) return 0;

    int count = 0;
    uint16_t stack[64];
    int top = 0;
    stack[top++] = 0;

    while (top > 0 && count < max_out) {
        uint16_t idx = stack[--top];
        const CollBvhNode& node = mesh.bvh_nodes[idx];
        AABB aabb = DequantAabb(mesh, node.aabb_min, node.aabb_max);

        if (!AABBOverlap(aabb, query)) continue;

        if (node.count_or_zero > 0) {
            int first = node.left_or_first;
            int nc    = node.count_or_zero;
            for (int ti = first; ti < first + nc && count < max_out; ++ti) {
                out_face_ids[count++] = ti;  // array index; face_id == ti by convention
            }
        } else {
            {
                uint16_t right_child = (uint16_t)(idx + node.left_or_first);
                uint16_t left_child  = (uint16_t)(idx + 1);
                if (top + 2 <= 64
                    && right_child < mesh.header->bvh_node_count
                    && left_child  < mesh.header->bvh_node_count) {
                    stack[top++] = right_child;
                    stack[top++] = left_child;
                }
            }
        }
    }
    return count;
}

// ---------------------------------------------------------------------------
// SurfaceOwnerOf
// ---------------------------------------------------------------------------

uint16_t SurfaceOwnerOf(const CollMesh& mesh, int face_id) {
    if (!mesh.surface_links || mesh.header->surface_link_count == 0)
        return INVALID_OWNER;
    int lo = 0, hi = (int)mesh.header->surface_link_count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        int key = (int)mesh.surface_links[mid].face_id;
        if (key == face_id) return mesh.surface_links[mid].owner_id;
        if (key < face_id)  lo = mid + 1;
        else                hi = mid - 1;
    }
    return INVALID_OWNER;
}

}  // namespace madeline_cube::physics
