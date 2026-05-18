#include <cassert>
#include <cstdio>

#include "../src/user/gameplay/physics/coll_mesh.hpp"
#include "../src/user/gameplay/world/level_loader.hpp"
#include "../src/user/gameplay/world/world.hpp"

using namespace madeline_cube;
using namespace madeline_cube::physics;

int main() {
    Room room;
    LevelGeometry geom;
    if (!LoadLevel("filesystem/lvl/first-room.lvl", room, geom)) {
        printf("[FAIL] LoadLevel\n"); return 1;
    }
    if (!room.coll_mesh) {
        printf("[FAIL] no colmesh\n"); return 1;
    }

    const auto& mesh = *room.coll_mesh;
    const float* o = mesh.header->quant_origin;
    const float qs = mesh.header->quant_scale;
    Vec3 spawn = room.player_start;
    Vec3 down = {0, -1, 0};

    printf("[test] player_start = (%.3f, %.3f, %.3f)\n", spawn.x, spawn.y, spawn.z);

    // 1. Floor under spawn
    RayHit floor_hit = RaycastMesh(mesh, spawn, down, 10.0f, BackfaceCull::Ignore);
    assert(floor_hit.hit);
    printf("[ok] floor under spawn: dist=%.3f normal=(%.2f,%.2f,%.2f)\n",
           floor_hit.t, floor_hit.normal.x, floor_hit.normal.y, floor_hit.normal.z);

    // 2. Climb wall tris exist with MAT_CLIMBABLE
    int climb_tris = 0;
    for (uint32_t i = 0; i < mesh.header->triangle_count; ++i)
        if (mesh.triangles[i].material & MAT_CLIMBABLE) climb_tris++;
    assert(climb_tris > 0);
    printf("[ok] climb tris: %d\n", climb_tris);

    // 3. Climb wall raycast (at x=18, y=[0.64,8.0])
    Vec3 wall_dir = {1, 0, 0};
    RayHit wh = RaycastMesh(mesh, {16, 4, 0}, wall_dir, 5.0f, BackfaceCull::Ignore);
    assert(wh.hit);
    printf("[ok] wall raycast: face_id=%d material=0x%04x normal=(%.2f,%.2f,%.2f)\n",
           wh.face_id, wh.material, wh.normal.x, wh.normal.y, wh.normal.z);

    // 4. Landing platform floor
    float land_x = o[0] + 750.0f * qs;
    RayHit land_hit = RaycastMesh(mesh, {land_x, spawn.y, 0}, down, 10.0f, BackfaceCull::Ignore);
    assert(land_hit.hit);
    printf("[ok] landing platform floor: dist=%.3f\n", land_hit.t);

    // 5. SurfaceOwnerOf: all static
    for (uint32_t i = 0; i < mesh.header->triangle_count; ++i)
        assert(SurfaceOwnerOf(mesh, (int)i) == INVALID_OWNER);
    printf("[ok] SurfaceOwnerOf: all %u → INVALID_OWNER\n", mesh.header->triangle_count);

    printf("\n[first_room_query_test] PASS\n");
    return 0;
}
