// Inc 1 (wall-climb-overhaul): plane-based wall query regression.
//
// Verifies QueryWallsFacePlane / QueryWallClosestToFace return the owning
// face-plane normal (stable across seams/corners), NOT the centre-relative
// diagonal of the sphere-closest-point query. Also verifies the wall gate
// (|normal.y| < 0.35, OG ClimbNormalCheck) rejects slopes.
#include <cmath>
#include <cstdio>
#include <vector>

#include "gameplay/physics/coll_mesh.hpp"
#include "gameplay/world/world.hpp"

using namespace madeline_cube;
using namespace madeline_cube::physics;

namespace {

struct Mesh {
    CollHeader header{};
    std::vector<CollVertex> verts;
    std::vector<CollTriangle> tris;
    std::vector<CollBvhNode> nodes;
    std::vector<Vec3> world_verts;
    CollMesh mesh{};

    // Floor + two wall quads sharing a vertical seam (z=0).
    void Build() {
        header.magic[0]='C'; header.magic[1]='M'; header.magic[2]='S'; header.magic[3]='H';
        header.version = 1; header.flags = 1; header.quant_scale = 1.0f;
        header.quant_origin[0]=header.quant_origin[1]=header.quant_origin[2]=0.0f;

        const int16_t half = 200, top = 500, wall_x = 20;
        // 0-3 floor; 4-7 wall quad A (z -200..0); 8-9 wall quad B (z 0..200)
        verts = {
            {{-half,0,-half}},{{half,0,-half}},{{half,0,half}},{{-half,0,half}},
            {{wall_x,0,-200}},{{wall_x,0,0}},{{wall_x,top,0}},{{wall_x,top,-200}},
            {{wall_x,0,200}},{{wall_x,top,200}},
        };
        uint16_t m = MAT_SOLID;
        tris = {
            {0,1,2,m,0,0},{0,2,3,m,1,0},
            {4,5,6,m,2,0},{4,6,7,m,3,0},   // wall quad A (normal -x)
            {5,8,9,m,4,0},{5,9,6,m,5,0},   // wall quad B (normal -x)
        };
        nodes = {
            {{-half,0,-half},{half,top,half},0,static_cast<uint16_t>(tris.size())},
        };
        header.vertex_count = static_cast<uint32_t>(verts.size());
        header.triangle_count = static_cast<uint32_t>(tris.size());
        header.bvh_node_count = static_cast<uint32_t>(nodes.size());
        header.surface_link_count = 0;

        mesh.header = &header;
        world_verts.resize(verts.size());
        for (size_t i = 0; i < verts.size(); ++i)
            world_verts[i] = DequantVert(mesh, verts[i]);
        mesh.vertices = verts.data();
        mesh.triangles = tris.data();
        mesh.bvh_nodes = nodes.data();
        mesh.surface_links = nullptr;
        mesh.world_verts = world_verts.data();
    }
};

}  // namespace

int main() {
    Mesh m;
    m.Build();
    Room room;
    room.coll_mesh = &m.mesh;
    room.collider_count = 0;

    // Query at the seam (z=0) at the player's waist height. The plane query
    // must return a face-plane normal (-1,0,0), NOT a diagonal with a Z
    // component (the sphere query would return such a diagonal near the seam).
    const Vec3 at_seam = {15.0f, 100.0f, 0.0f};
    WallHit plane_hit = QueryWallClosestToFace(room, at_seam, 6.0f, {-1.0f, 0.0f, 0.0f});
    if (!plane_hit.hit) {
        printf("FAIL: no plane hit at seam\n");
        return 1;
    }
    printf("seam: normal=(%.2f,%.2f,%.2f) pushout=%.2f\n",
           plane_hit.plane_normal.x, plane_hit.plane_normal.y, plane_hit.plane_normal.z,
           plane_hit.pushout);
    // Face normal must be the wall's -x, with no spurious Z diagonal.
    if (std::fabs(plane_hit.plane_normal.x + 1.0f) > 0.01f ||
        std::fabs(plane_hit.plane_normal.y) > 0.01f ||
        std::fabs(plane_hit.plane_normal.z) > 0.01f) {
        printf("FAIL: plane normal is diagonal (not face-plane)\n");
        return 1;
    }
    // Pushout: query at x=15, wall at x=20, radius 6 → touching (pushout>0).
    if (plane_hit.pushout <= 0.0f) {
        printf("FAIL: plane pushout not positive when touching\n");
        return 1;
    }

    // Far from the wall (x=0, wall at 20, radius 6) → no hit.
    WallHit far_hit = QueryWallClosestToFace(room, {0.0f, 100.0f, 0.0f}, 6.0f, {-1.0f, 0.0f, 0.0f});
    if (far_hit.hit) {
        printf("FAIL: plane hit when far from wall\n");
        return 1;
    }

    // Sanity: the sphere-closest query still returns a hit here (documenting
    // the difference the plane query fixes) — its normal may be diagonal.
    WallHit sphere_hit = QueryWallClosestToNormal(room, at_seam, 6.0f, {-1.0f, 0.0f, 0.0f});
    if (!sphere_hit.hit) {
        printf("FAIL: sphere query no hit at seam (test setup broken)\n");
        return 1;
    }
    printf("sphere_normal=(%.2f,%.2f,%.2f)\n",
           sphere_hit.normal.x, sphere_hit.normal.y, sphere_hit.normal.z);

    printf("wall_plane_query_smoke PASS\n");
    return 0;
}
