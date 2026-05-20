#include <cassert>
#include <cmath>
#include <cstring>

#include "../src/user/gameplay/physics/coll_mesh.hpp"
#include "../src/user/gameplay/world/level_loader.hpp"
#include "../src/user/gameplay/world/world.hpp"

using namespace madeline_cube;

static bool NearlyEq(float a, float b) { return std::fabs(a - b) < 1e-3f; }

int main() {
    {
        Room room;
        LevelGeometry geometry;
        const bool ok = LoadLevel("filesystem/lvl/first-room.lvl", room, geometry);
        assert(ok && "LoadLevel failed — run baker first: python3 tools/bake_map.py ...");

        // Static colliders now loaded via .colmesh; runtime collider_count is 0.
        assert(room.collider_count == 0 && "static colliders now loaded via .colmesh");

        // PlayerSpawn coordinate fixture (port of first-room map origin 0, 0, 64)
        assert(NearlyEq(room.player_start.x, 0.0f));
        assert(NearlyEq(room.player_start.y, 12.8f));
        assert(NearlyEq(room.player_start.z, 0.0f));

        // Geometry loaded
        assert(geometry.face_count > 0);
        assert(geometry.vertex_count > 0);
        assert(room.coll_mesh && "LoadLevel should attach the baked first-room.colmesh sidecar");

        if (room.coll_mesh) physics::FreeCollMesh(room.coll_mesh);
    }

    {
        Room room;
        LevelGeometry geometry;
        const bool ok = LoadLevel("filesystem/lvl/1-1.lvl", room, geometry);
        assert(ok && "LoadLevel should load the baked 1-1 shell");
        assert(room.collider_count == 0);
        assert(geometry.face_count > 102);
        assert(geometry.vertex_count >= 400);
        assert(room.spawn_count == 1 && "1-1 shell bake should currently spawn only Strawberry");
        assert(std::strcmp(room.skybox, "bsides") == 0);
        assert(std::strcmp(room.music, "mus_lvl1_bside") == 0);
        assert(std::strcmp(room.ambience, "mountain") == 0);
        assert(NearlyEq(room.snow_amount, 0.5f));
        assert(NearlyEq(room.snow_dir.x, 0.0f));
        assert(NearlyEq(room.snow_dir.y, 1.0f));
        assert(NearlyEq(room.snow_dir.z, 0.0f));
        assert(room.has_cassette);
        assert(NearlyEq(room.cassette.x, -36.8f));
        assert(NearlyEq(room.cassette.y, 100.8f));
        assert(NearlyEq(room.cassette.z, 12.8f));
        assert(room.coll_mesh && "LoadLevel should attach the baked 1-1.colmesh sidecar");

        if (room.coll_mesh) physics::FreeCollMesh(room.coll_mesh);
    }
    return 0;
}
