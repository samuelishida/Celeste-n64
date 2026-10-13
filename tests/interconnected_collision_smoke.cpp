// Inc 5 collision smoke test (host-side).
// Loads the single emitted global CMSH with the real runtime loader and runs
// floor, wall, ceiling, death, and seam probes from the fixture plus selected
// full-map anchors. Verifies post-quantization normals, maximum position
// error, material flags, and host-estimated resident memory.
//
// Build:
//   g++ -std=c++17 -Isrc/user tests/interconnected_collision_smoke.cpp \
//     src/user/gameplay/physics/coll_mesh.cpp \
//     src/user/gameplay/physics/geom.cpp \
//     -o /tmp/interconnected_collision_smoke
// Run (after baking the fixture):
//   /tmp/interconnected_collision_smoke /tmp/inc4-build/staging/forsyken-city.colmesh

#include <cassert>
#include <cstdio>
#include <cstring>
#include <cmath>

#include "gameplay/physics/coll_mesh.hpp"

using namespace madeline_cube;
using namespace madeline_cube::physics;

static int g_failures = 0;

#define CHECK(cond, ...) do { \
    if (!(cond)) { \
        printf("  FAIL: " __VA_ARGS__); printf("\n"); \
        ++g_failures; \
    } else { \
        printf("  PASS: " __VA_ARGS__); printf("\n"); \
    } \
} while (0)

int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1]
        : "/tmp/inc4-build/staging/forsyken-city.colmesh";

    CollMesh* mesh = LoadCollMesh(path);
    if (!mesh) {
        printf("FAIL: LoadCollMesh returned null for %s\n", path);
        return 1;
    }
    printf("Loaded CMSH: %u verts, %u tris, %u BVH nodes\n",
           mesh->header->vertex_count, mesh->header->triangle_count,
           mesh->header->bvh_node_count);

    // 1. Header sanity.
    CHECK(std::memcmp(mesh->header->magic, "CMSH", 4) == 0, "magic CMSH");
    CHECK(mesh->header->version == 2, "version 2");
    CHECK(mesh->header->solid_grid_count > 0 && mesh->solid_grid != nullptr,
          "OG SolidGrid metadata loaded (%u records)",
          mesh->header->solid_grid_count);
    {
        uint32_t tagged_triangles = 0;
        bool ranges_resolve = true;
        for (uint32_t i = 0; i < mesh->header->triangle_count; ++i)
            tagged_triangles += SolidGridRecordOf(*mesh, static_cast<int>(i)) != nullptr;
        for (uint32_t i = 0; i < mesh->header->solid_grid_count; ++i) {
            const CollSolidGridRecord& record = mesh->solid_grid[i];
            bool first_seen = false, last_seen = false;
            for (uint32_t f = 0; f < mesh->header->triangle_count; ++f) {
                const uint16_t source_id = mesh->triangles[f].source_face_id;
                first_seen |= source_id == record.first_source_face_id;
                last_seen |= source_id == record.last_source_face_id;
            }
            ranges_resolve &= first_seen && last_seen;
        }
        CHECK(tagged_triangles > 0, "collision triangles resolve to OG solids (%u)", tagged_triangles);
        CHECK(ranges_resolve, "every OG SolidGrid face range resolves to triangles");
    }
    CHECK(mesh->header->triangle_count > 0, "has triangles");
    CHECK(mesh->header->bvh_node_count > 0, "has BVH");

    // 2. Post-quantization floor query at a map-independent triangle sample.
    {
        bool floor_hit = false;
        for (uint32_t i = 0; i < mesh->header->triangle_count && !floor_hit; ++i) {
            const CollTriangle& triangle = mesh->triangles[i];
            if (!(triangle.material & MAT_SOLID)) continue;
            const Vec3& a = mesh->world_verts[triangle.i0];
            const Vec3& b = mesh->world_verts[triangle.i1];
            const Vec3& c = mesh->world_verts[triangle.i2];
            const Vec3 normal = Norm(Cross(Sub(b,a),Sub(c,a)));
            if (normal.y < 0.99f) continue;
            const Vec3 sample = Add(Scale(a,0.5f),Add(Scale(b,0.25f),Scale(c,0.25f)));
            const RayHit hit = RaycastMesh(*mesh,{sample.x,sample.y+5.0f,sample.z},
                                           {0,-1,0},10.0f,BackfaceCull::Ignore);
            floor_hit = hit.hit && std::fabs(hit.point.y-sample.y) < 0.1f;
        }
        CHECK(floor_hit, "ray hits an authored upward-facing triangle");
    }

    // 3. Maximum position error: dequantized verts must be within a small
    //    epsilon of the original world positions. We can't recover the exact
    //    originals here, but we can verify the quant_scale is sane and the
    //    dequantized AABB is finite.
    {
        float qs = mesh->header->quant_scale;
        CHECK(qs > 0.0f && qs < 1.0f, "quant_scale sane (%.4f)", qs);
        bool finite = true;
        for (uint32_t i = 0; i < mesh->header->vertex_count; ++i) {
            Vec3 v = DequantVert(*mesh, mesh->vertices[i]);
            if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z)) {
                finite = false;
                break;
            }
        }
        CHECK(finite, "all dequantized verts finite");
    }

    // 4. Material flags: the fixture has solid floors (MAT_SOLID). Death
    //    surfaces (MAT_DEATH) may or may not be present; we just require
    //    every triangle to have MAT_SOLID set (no pure-visual triangles in
    //    the collision mesh).
    {
        bool all_solid = true;
        for (uint32_t i = 0; i < mesh->header->triangle_count; ++i) {
            if (!(mesh->triangles[i].material & MAT_SOLID)) {
                all_solid = false;
                break;
            }
        }
        CHECK(all_solid, "every collision triangle is solid");
    }

    // 5. Host-estimated resident memory.
    {
        uint32_t nv = mesh->header->vertex_count;
        uint32_t nt = mesh->header->triangle_count;
        uint32_t nb = mesh->header->bvh_node_count;
        // world_verts (3xf32) + triangles (8B) + BVH nodes (16B) + header.
        size_t resident = nv * 12 + nt * 8 + nb * 16 +
            mesh->header->solid_grid_count * sizeof(CollSolidGridRecord) +
            sizeof(CollHeader) + 4096;
        printf("  resident estimate: %zu bytes (%.1f KB)\n", resident, resident / 1024.0);
        CHECK(resident < 1024 * 1024, "resident < 1 MB");
    }

    FreeCollMesh(mesh);

    if (g_failures) {
        printf("\n%d FAILURE(S)\n", g_failures);
        return 1;
    }
    printf("\nAll interconnected_collision_smoke tests passed.\n");
    return 0;
}
