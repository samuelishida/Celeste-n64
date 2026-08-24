// Host-side autowalk-distance contract test (streaming-experience Inc 3,
// Pattern C: links mappack_loader.cpp, reads the baked manifest).
//
// The preload/async captures run `kDebugAutoWalk` at `kDebugAutoWalkDistance`
// = 260.0 world units so the walk is guaranteed to cross at least one cell
// seam (a cell is `chunk_size * scale` = 1200 * 0.2 = 240 world units; walking
// 260 > 240 in a straight line from any spawn crosses at least one grid line).
// This test asserts that guarantee against the REAL manifest (so the 260.0 is
// not a magic number): it loads the Start spawn + cell size and verifies
// 260.0 > cell_size, and that the spawn lies within its cell.
//
// Build:
//   g++ -std=c++17 -Isrc/user tests/autowalk_distance_contract.cpp \
//     src/user/gameplay/world/mappack_loader.cpp
// Run:
//   /tmp/autowalk_distance_contract <mappack_path>
#include <cassert>
#include <cstdio>
#include <cstring>

#include "gameplay/world/mappack_loader.hpp"
#include "gameplay/render/render_origin_math.hpp"  // ResolveCellIndex (same convention as MapRuntime)

using namespace madeline_cube;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <mappack_path>\n", argv[0]);
        return 2;
    }
    MapSpecV2 spec;
    if (!LoadMapPackV2(argv[1], spec)) {
        std::fprintf(stderr, "FAIL: cannot load mappack %s\n", argv[1]);
        return 1;
    }
    const V2SpawnSpec* start = spec.FindStartSpawn();
    if (!start) {
        std::fprintf(stderr, "FAIL: no Start spawn in manifest\n");
        return 1;
    }
    const V2RoomSpec* room = spec.FindRoom(start->room_id);
    if (!room) {
        std::fprintf(stderr, "FAIL: Start room %s not found\n", start->room_id);
        return 1;
    }

    const float cell_size = spec.chunk_size * spec.scale;  // 240 world units
    const float walk = 260.0f;  // the capture value (kDebugAutoWalkDistance)
    std::printf("cell_size=%.1f walk=%.1f spawn=(%.1f,%.1f,%.1f) room=%s\n",
                cell_size, walk, start->position.x, start->position.y,
                start->position.z, start->room_id);

    // The walk must exceed the cell size so ≥1 seam crossing is guaranteed
    // from any spawn (the max distance to the next grid line in any direction
    // is ≤ cell_size).
    assert(walk > cell_size);

    // The spawn must lie within its room's cell — the real check the earlier
    // tautology was supposed to be. Two parts: (a) the spawn's XZ position is
    // inside the room's world AABB (the cell footprint; Y is ignored, the
    // spawn sits on the floor of the cell), and (b) the canonical cell-index
    // formula (the one MapRuntime::ResolveCellByPosition uses) maps the spawn
    // back to exactly this room's (cell_ix, cell_iz). The autowalk starts
    // inside a cell, so both must hold.
    {
        // Inline XZ containment (min/max on min/max) so this test links
        // mappack_loader.cpp only — AABB::IntersectsXZ lives in a separate TU.
        const AABB& aabb = room->world_aabb;
        const float px = start->position.x, pz = start->position.z;
        const bool in_xz = px >= aabb.min.x && px <= aabb.max.x &&
                           pz >= aabb.min.z && pz <= aabb.max.z;
        assert(in_xz);
        int ix = 0, iz = 0;
        const bool resolved = ResolveCellIndex(start->position,
                                               spec.chunk_size, spec.scale, ix, iz);
        assert(resolved);
        assert(ix == room->cell_ix && iz == room->cell_iz);
    }
    std::printf("PASS: autowalk distance %.1f > cell size %.1f guarantees ≥1 seam crossing\n",
                walk, cell_size);
    std::printf("PASS: spawn lies within its cell (AABB + canonical cell index)\n");
    std::printf("ALL PASS\n");
    return 0;
}
