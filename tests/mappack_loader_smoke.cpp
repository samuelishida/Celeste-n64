// Map-pack loader smoke test (host-side).
// Loads the binary .mappack produced by bake_map_pack.py and asserts the
// MapSpec fields parse correctly. Requires the bake to have run first.
//
// Build:
//   g++ -std=c++17 -Isrc/user tests/mappack_loader_smoke.cpp \
//     src/user/gameplay/world/mappack_loader.cpp -o /tmp/mappack_loader_smoke
// Run (after baking):
//   /tmp/mappack_loader_smoke /tmp/madeline-mappack-smoke/forsyken-city.mappack

#include <cassert>
#include <cstdio>
#include <cstring>

#include "gameplay/world/mappack_loader.hpp"

using namespace madeline_cube;

int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1]
        : "/tmp/madeline-mappack-smoke/forsyken-city.mappack";

    MapSpec spec;
    if (!LoadMapPack(path, spec)) {
        fprintf(stderr, "FAIL: LoadMapPack returned false for %s\n", path);
        return 1;
    }

    // Room count within cap.
    assert(spec.room_count > 1);
    assert(spec.room_count <= MapSpec::kMaxRooms);

    // Start room resolves.
    assert(spec.start_room_id[0] != '\0');
    const MapRoomSpec* start = spec.FindRoom(spec.start_room_id);
    assert(start != nullptr);
    assert(start->has_start_spawn);

    // Atmosphere is shared.
    assert(std::strncmp(spec.atmosphere_skybox, "city", 4) == 0);
    assert(std::strncmp(spec.atmosphere_music, "mus_lvl1", 8) == 0);

    // Scale + chunk_size round-tripped.
    assert(spec.scale > 0.19f && spec.scale < 0.21f);
    assert(spec.chunk_size > 1199.0f && spec.chunk_size < 1201.0f);

    // DFS paths match the rom:/lvl/forsyken-city/ layout.
    for (int i = 0; i < spec.room_count; ++i) {
        const auto& r = spec.rooms[i];
        assert(std::strncmp(r.lvl_path, "rom:/lvl/forsyken-city/", 22) == 0);
        // Adjacency symmetry: if neighbor +X is set, that neighbor's -X is us.
        if (r.neighbors[0][0] != '\0') {  // +X
            const MapRoomSpec* nb = spec.FindRoom(r.neighbors[0]);
            assert(nb != nullptr);
            assert(std::strncmp(nb->neighbors[1], r.id, MapRoomSpec::kIdLen) == 0);
        }
    }

    printf("PASS: mappack_loader_smoke — %d rooms, start=%s, skybox=%s\n",
           spec.room_count, spec.start_room_id, spec.atmosphere_skybox);
    return 0;
}