#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "../src/user/gameplay/physics/coll_mesh.hpp"
#include "../src/user/gameplay/world/level_loader.hpp"
#include "../src/user/gameplay/world/world.hpp"

using namespace madeline_cube;

static void PrintShellAuditFixture(const char* path) {
    FILE* f = fopen(path, "r");
    if (!f) {
        printf("[queries] shell audit fixture missing: %s\n", path);
        return;
    }

    printf("[queries] shell audit fixture: %s\n", path);
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        int len = static_cast<int>(strlen(line));
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[--len] = '\0';
        }
        if (len == 0 || line[0] == '#') continue;
        printf("[queries] probe %s\n", line);
    }
    fclose(f);
}

int main(int argc, char* argv[]) {
    PrintShellAuditFixture("tests/fixtures/1-1-shell-boundary.txt");

    const char* lvl_path = "filesystem/lvl/first-room.lvl";
    if (argc > 1) lvl_path = argv[1];

    Room* room_ptr = new Room;
    LevelGeometry* geom_ptr = new LevelGeometry;
    Room& room = *room_ptr;
    LevelGeometry& geom = *geom_ptr;
    if (!LoadLevel(lvl_path, room, geom)) { printf("LoadLevel failed\n"); return 1; }
    if (!room.coll_mesh) { printf("colmesh not loaded — run 'make bake-colmesh LEVEL=first-room' first\n"); return 1; }

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
    float wmx[3]  = { ox + room.coll_mesh->header->aabb_max[0]*qs,
                      oy + room.coll_mesh->header->aabb_max[1]*qs,
                      oz + room.coll_mesh->header->aabb_max[2]*qs };

    constexpr float kRayDist = 20.0f;

    // Sanity: 1000 floor queries from the baked spawn location — must hit the
    // floor directly below the room's canonical PlayerSpawn.
    int floor_hit = 0;
    const Vec3 floor_origin = {room.player_start.x, room.player_start.y + 5.0f, room.player_start.z};
    for (int i = 0; i < 1000; ++i) {
        GroundHit gh = QueryFloorSource(room, floor_origin, kRayDist);
        if (gh.hit) floor_hit++;
    }
    printf("[queries] floor hits from spawn: %d/1000\n", floor_hit);
    assert(floor_hit == 1000 && "floor query must hit from above spawn");

    // Sanity: wall probe just inside the back boundary at player height must hit.
    {
        const Vec3 wall_origin = {
            room.player_start.x,
            room.player_start.y,
            wmx[2] - 1.0f,
        };
        WallHit hits[kMaxWallHits];
        int n = QueryWalls(room, wall_origin, 2.0f, hits, kMaxWallHits);
        printf("[queries] wall hits near back boundary: %d\n", n);
        assert(n > 0 && "wall query must hit the back boundary");
    }

    // Sanity: ceiling above the spawn still reports a hit somewhere in the room.
    {
        Vec3 origin = {room.player_start.x, room.player_start.y - 5.0f, room.player_start.z};
        CeilingHit ch = QueryCeilingSource(room, origin, kRayDist);
        printf("[queries] ceiling from below: hit=%d\n", (int)ch.hit);
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
