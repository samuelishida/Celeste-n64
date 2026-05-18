#include <cassert>
#include <cmath>

#include "../src/user/gameplay/physics/coll_mesh.hpp"
#include "../src/user/gameplay/world/level_loader.hpp"
#include "../src/user/gameplay/world/world.hpp"

using namespace madeline_cube;

static bool NearlyEq(float a, float b) { return std::fabs(a - b) < 1e-3f; }

int main() {
    Room room;
    LevelGeometry geometry;
    const bool ok = LoadLevel("filesystem/lvl/1-1.lvl", room, geometry);
    assert(ok && "LoadLevel failed — run baker first: python3 tools/bake_map.py ...");

    // Static colliders now loaded via .colmesh; runtime collider_count is 0.
    assert(room.collider_count == 0 && "static colliders now loaded via .colmesh");

    // PlayerSpawn coordinate fixture (port of map origin 32, 120, 384)
    assert(NearlyEq(room.player_start.x, 0.64f));
    assert(NearlyEq(room.player_start.y, 7.68f));
    assert(NearlyEq(room.player_start.z, -2.40f));

    // Geometry loaded
    assert(geometry.face_count > 0);
    assert(geometry.vertex_count > 0);

    if (room.coll_mesh) physics::FreeCollMesh(room.coll_mesh);
    return 0;
}
