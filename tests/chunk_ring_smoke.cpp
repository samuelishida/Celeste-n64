// Host-side neighbor-ring resolution test (Inc 2).
//
// `ChunkRingRenderer` itself is N64-only (it loads `LvlRoomRenderer`, which
// includes libdragon/t3d). This test exercises the pure `ResolveRingRooms`
// helper (inline in the header) against a real baked v2 manifest, asserting
// the ring resolves the center + its up-to-4 neighbors correctly, including
// map-edge cells with fewer than 4 neighbors.
//
// Build:
//   g++ -std=c++17 -Isrc/user tests/chunk_ring_smoke.cpp \
//     src/user/gameplay/world/mappack_loader.cpp \
//     -o /tmp/chunk_ring_smoke
// Run (after baking the fixture):
//   /tmp/chunk_ring_smoke /tmp/inc4-build/staging/forsyken-city.mappack

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

#include "gameplay/render/chunk_ring_renderer.hpp"

using namespace madeline_cube;

int main(int argc, char** argv) {
    const char* mappack_path = argc > 1 ? argv[1]
        : "/tmp/inc4-build/staging/forsyken-city.mappack";

    MapSpecV2 spec;
    assert(LoadMapPackV2(mappack_path, spec));
    assert(spec.room_count > 0);

    // 1. The start room resolves a ring of center + neighbors.
    {
        const V2RoomSpec* start = spec.FindRoom(spec.start_room_id);
        assert(start != nullptr);
        const V2RoomSpec* ring[5] = {};
        int n = ResolveRingRooms(spec, *start, ring);
        assert(n >= 1 && n <= 5);
        assert(ring[0] == start);  // center is always first
        printf("PASS: start room %s ring count = %d\n", start->id, n);
    }

    // 2. Every room's ring is internally consistent: each neighbor resolves
    //    to a real room in the manifest, and the center is always present.
    {
        for (int i = 0; i < spec.room_count; ++i) {
            const V2RoomSpec& room = spec.rooms[i];
            const V2RoomSpec* ring[5] = {};
            int n = ResolveRingRooms(spec, room, ring);
            assert(n >= 1);
            assert(ring[0] == &room);
            for (int k = 1; k < n; ++k) {
                // Each resolved neighbor must be a distinct manifest room.
                assert(ring[k] != &room);
                bool found = false;
                for (int j = 0; j < spec.room_count; ++j) {
                    if (spec.rooms[j].id[0] != '\0' &&
                        std::strncmp(spec.rooms[j].id, ring[k]->id,
                                     V2RoomSpec::kIdLen) == 0) {
                        found = true;
                        break;
                    }
                }
                assert(found);
            }
        }
        printf("PASS: all %d rooms resolve consistent rings\n", spec.room_count);
    }

    // 3. A map-edge cell (if any) has fewer than 4 neighbors.
    {
        bool saw_edge = false;
        for (int i = 0; i < spec.room_count; ++i) {
            const V2RoomSpec& room = spec.rooms[i];
            int neighbor_count = 0;
            for (int a = 0; a < 4; ++a) {
                if (room.neighbors[a][0] != '\0') ++neighbor_count;
            }
            if (neighbor_count < 4) {
                saw_edge = true;
                const V2RoomSpec* ring[5] = {};
                int n = ResolveRingRooms(spec, room, ring);
                assert(n == neighbor_count + 1);  // center + present neighbors
                printf("PASS: edge cell %s has %d neighbors (ring=%d)\n",
                       room.id, neighbor_count, n);
            }
        }
        assert(saw_edge);  // the map must have at least one edge cell
    }

    printf("ALL PASS\n");
    return 0;
}
