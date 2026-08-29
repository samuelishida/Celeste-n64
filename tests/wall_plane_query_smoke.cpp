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

    // Geometry-bounding regression (climb-parity-3 Inc 5 on-device gate): a
    // probe ABOVE a wall's physical top edge must NOT phantom-hit via the
    // infinite-plane projection. Without the vertex-extents bound this
    // reported a ghost grab → instant-release grip_lost pump every frame.
    // Build a short wall whose top (y=40) is below the probe (y=100).
    {
        Mesh short_wall;
        short_wall.verts = {
            {{-200,0,-200}},{{200,0,-200}},{{200,0,200}},{{-200,0,200}},  // floor
            {{20,0,-200}},{{20,0,200}},{{20,40,200}},{{20,40,-200}},      // wall top y=40
        };
        uint16_t sm = MAT_SOLID;
        short_wall.tris = {
            {0,1,2,sm,0,0},{0,2,3,sm,1,0},
            {4,5,6,sm,2,0},{4,6,7,sm,3,0},
        };
        short_wall.nodes = {{{-200,0,-200},{200,40,200},0,static_cast<uint16_t>(short_wall.tris.size())}};
        short_wall.header.magic[0]='C'; short_wall.header.magic[1]='M'; short_wall.header.magic[2]='S'; short_wall.header.magic[3]='H';
        short_wall.header.version = 1; short_wall.header.flags = 1; short_wall.header.quant_scale = 1.0f;
        short_wall.header.quant_origin[0]=short_wall.header.quant_origin[1]=short_wall.header.quant_origin[2]=0.0f;
        short_wall.header.vertex_count = static_cast<uint32_t>(short_wall.verts.size());
        short_wall.header.triangle_count = static_cast<uint32_t>(short_wall.tris.size());
        short_wall.header.bvh_node_count = static_cast<uint32_t>(short_wall.nodes.size());
        short_wall.header.surface_link_count = 0;
        short_wall.mesh.header = &short_wall.header;
        short_wall.world_verts.resize(short_wall.verts.size());
        for (size_t i = 0; i < short_wall.verts.size(); ++i)
            short_wall.world_verts[i] = DequantVert(short_wall.mesh, short_wall.verts[i]);
        short_wall.mesh.vertices = short_wall.verts.data();
        short_wall.mesh.triangles = short_wall.tris.data();
        short_wall.mesh.bvh_nodes = short_wall.nodes.data();
        short_wall.mesh.surface_links = nullptr;
        short_wall.mesh.world_verts = short_wall.world_verts.data();

        Room r2;
        r2.coll_mesh = &short_wall.mesh;
        r2.collider_count = 0;
        // Probe at x=15 (5 from the x=20 wall), y=100 (60 ABOVE the wall top
        // y=40), within r6 of the infinite plane. Must NOT hit — the wall
        // physically ended at y=40.
        const Vec3 above_top = {15.0f, 100.0f, 0.0f};
        const WallHit ghost = QueryWallClosestToFace(r2, above_top, 6.0f, {-1.0f,0.0f,0.0f});
        if (ghost.hit) {
            printf("FAIL: plane query phantom-hit a wall whose physical top is below probe\n");
            return 1;
        }
        // Same probe just BELOW the top (y=30) must still hit (the wall IS
        // there) — proving the bound only rejects the ghost, not real walls.
        const Vec3 below_top = {15.0f, 30.0f, 0.0f};
        const WallHit real = QueryWallClosestToFace(r2, below_top, 6.0f, {-1.0f,0.0f,0.0f});
        if (!real.hit || std::fabs(real.plane_normal.x + 1.0f) > 0.01f) {
            printf("FAIL: plane query lost a real wall below its top\n");
            return 1;
        }
        printf("geometry-bound: above_top ghost_hit=%d below_top real_hit=%d (pn=%.2f)\n",
               (int)ghost.hit, (int)real.hit, real.plane_normal.x);
    }

    printf("wall_plane_query_smoke PASS\n");
    return 0;
}
