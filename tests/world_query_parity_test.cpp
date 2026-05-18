#include <cassert>
#include <cmath>
#include <cstdio>

#include "../src/user/gameplay/physics/coll_mesh.hpp"
#include "../src/user/gameplay/world/level_loader.hpp"
#include "../src/user/gameplay/world/world.hpp"

using namespace madeline_cube;

static float Dot(const Vec3& a, const Vec3& b) {
    return a.x*b.x + a.y*b.y + a.z*b.z;
}

int main(int argc, char* argv[]) {
    const char* lvl_path = "filesystem/lvl/1-1.lvl";
    if (argc > 1) lvl_path = argv[1];

    Room* room_ptr = new Room;
    LevelGeometry* geom_ptr = new LevelGeometry;
    Room& room = *room_ptr;
    LevelGeometry& geom = *geom_ptr;
    if (!LoadLevel(lvl_path, room, geom)) { printf("LoadLevel failed\n"); return 1; }
    if (!room.coll_mesh) { printf("colmesh not loaded — run 'make bake-colmesh LEVEL=1-1' first\n"); return 1; }

    printf("[queries] loaded: %u colmesh tris, %d dynamic colliders\n",
           room.coll_mesh->header->triangle_count, room.collider_count);

    // World AABB from colmesh quant bounds
    const float qs = room.coll_mesh->header->quant_scale;
    const float ox = room.coll_mesh->header->quant_origin[0];
    const float oy = room.coll_mesh->header->quant_origin[1];
    const float oz = room.coll_mesh->header->quant_origin[2];
    float wmin[3] = { ox + room.coll_mesh->header->aabb_min[0]*qs,
                      oy + room.coll_mesh->header->aabb_min[1]*qs,
                      oz + room.coll_mesh->header->aabb_min[2]*qs };
    float wmax[3] = { ox + room.coll_mesh->header->aabb_max[0]*qs,
                      oy + room.coll_mesh->header->aabb_max[1]*qs,
                      oz + room.coll_mesh->header->aabb_max[2]*qs };

    constexpr float kRayDist = 20.0f;

    // Sanity: 1000 floor queries from grid — must find floor below level center
    int floor_hit = 0;
    const float cx = (wmin[0] + wmax[0]) * 0.5f;
    const float cz = (wmin[2] + wmax[2]) * 0.5f;
    const float cy = wmax[1] - 1.0f;
    for (int i = 0; i < 1000; ++i) {
        Vec3 origin = {cx, cy, cz};
        GroundHit gh = QueryFloorSource(room, origin, kRayDist);
        if (gh.hit) floor_hit++;
    }
    printf("[queries] floor hits from center: %d/1000\n", floor_hit);
    assert(floor_hit == 1000 && "floor query must hit from above center");

    // Sanity: ceiling above center
    {
        Vec3 origin = {cx, wmin[1] + 1.0f, cz};
        CeilingHit ch = QueryCeilingSource(room, origin, kRayDist);
        printf("[queries] ceiling from below: hit=%d\n", (int)ch.hit);
    }

    // Sanity: walls near level center
    {
        WallHit hits[kMaxWallHits];
        int n = QueryWalls(room, {cx, cy, cz}, 0.5f, hits, kMaxWallHits);
        printf("[queries] walls near center: %d\n", n);
    }

    // Verify SurfaceOwnerOf returns INVALID_OWNER for all triangles (static level)
    {
        using namespace madeline_cube::physics;
        const CollMesh& cm = *room.coll_mesh;
        for (uint32_t i = 0; i < cm.header->triangle_count; ++i) {
            assert(SurfaceOwnerOf(cm, static_cast<int>(i)) == INVALID_OWNER);
        }
        printf("[queries] SurfaceOwnerOf: all %u → INVALID_OWNER OK\n",
               cm.header->triangle_count);
    }

    delete geom_ptr;
    delete room_ptr;

    printf("[queries] PASS\n");
    return 0;
}