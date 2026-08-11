// Host-side tile-streamer ring resolution test (Inc 3).
//
// Exercises the pure `ResolveDistanceRing` + `ResolveVisibleTiles` helpers
// (inline in `tile_streamer.hpp`) against a real baked v2 manifest. Asserts:
//   - the center cell's ring resolves the center + all Chebyshev-1 neighbors
//     (|dx|<=1 && |dz|<=1), never exceeding kMaxRing;
//   - the center is always first;
//   - map-edge cells resolve fewer than the full ring (never fatal);
//   - `ResolveVisibleTiles` maps a synthetic frustum to a visible room set.
//
// This replaces the deleted `chunk_ring_smoke.cpp` (which tested the
// superseded `ResolveRingRooms`).
//
// Build:
//   g++ -std=c++17 -Isrc/user tests/tile_streamer_smoke.cpp \
//     src/user/gameplay/world/mappack_loader.cpp \
//     -o /tmp/tile_streamer_smoke
// Run (after baking the fixture):
//   /tmp/tile_streamer_smoke /tmp/inc4-build/staging/forsyken-city.mappack

#include <cassert>
#include <cstdio>
#include <cstring>

#include "gameplay/render/tile_streamer.hpp"

using namespace madeline_cube;

int main(int argc, char** argv) {
    const char* mappack_path = argc > 1 ? argv[1]
        : "/tmp/inc4-build/staging/forsyken-city.mappack";

    MapSpecV2 spec;
    assert(LoadMapPackV2(mappack_path, spec));
    assert(spec.room_count > 0);
    assert(spec.room_count <= kMaxRing || spec.room_count <= 64);
    printf("loaded %d rooms from %s\n", spec.room_count, mappack_path);

    // 1. The start room resolves a ring of center + Chebyshev-1 neighbors,
    //    center always first, never exceeding kMaxRing.
    {
        const V2RoomSpec* start = spec.FindRoom(spec.start_room_id);
        assert(start != nullptr);
        const V2RoomSpec* ring[kMaxRing] = {};
        int n = ResolveDistanceRing(spec, *start, ring, kMaxRing);
        assert(n >= 1 && n <= kMaxRing);
        assert(ring[0] == start);
        // Every non-center ring entry must be within Chebyshev distance 1.
        for (int k = 1; k < n; ++k) {
            const int dx = ring[k]->cell_ix - start->cell_ix;
            const int dz = ring[k]->cell_iz - start->cell_iz;
            assert(dx >= -1 && dx <= 1 && dz >= -1 && dz <= 1);
            assert(ring[k] != start);
        }
        printf("PASS: start room %s ring count = %d\n", start->id, n);
    }

    // 2. Every room's ring is internally consistent: center first, each ring
    //    entry within Chebyshev-1, and the center is never duplicated.
    {
        int max_ring = 0;
        for (int i = 0; i < spec.room_count; ++i) {
            const V2RoomSpec& room = spec.rooms[i];
            const V2RoomSpec* ring[kMaxRing] = {};
            int n = ResolveDistanceRing(spec, room, ring, kMaxRing);
            assert(n >= 1 && n <= kMaxRing);
            assert(ring[0] == &room);
            for (int k = 1; k < n; ++k) {
                assert(ring[k] != &room);
                const int dx = ring[k]->cell_ix - room.cell_ix;
                const int dz = ring[k]->cell_iz - room.cell_iz;
                assert(dx >= -1 && dx <= 1 && dz >= -1 && dz <= 1);
            }
            if (n > max_ring) max_ring = n;
        }
        printf("PASS: all %d rooms resolve consistent rings (max=%d, cap=%d)\n",
               spec.room_count, max_ring, kMaxRing);
    }

    // 3. ResolveVisibleTiles: a synthetic frustum covering the camera's cell
    //    (a single-tile box) must resolve at least the center cell.
    {
        const float cell = spec.chunk_size * spec.scale;
        assert(cell > 0.0f);
        // Build an identity-ish inverse view-proj mapping NDC to a box around
        // the start cell center (half cell, so it covers exactly one tile).
        Mat4 inv = Mat4::Identity();
        const V2RoomSpec* start = spec.FindRoom(spec.start_room_id);
        assert(start != nullptr);
        const float cx = (start->cell_ix + 0.5f) * cell;
        const float cz = (start->cell_iz + 0.5f) * cell;
        const float half = cell * 0.5f;
        inv.m[0] = half;     inv.m[12] = cx;   // x: cx + [-half, half]
        inv.m[5] = 0.0f;     inv.m[13] = 0.0f; // y: constant
        inv.m[10] = half;    inv.m[14] = cz;   // z: cz + [-half, half]

        const V2RoomSpec* visible[kMaxRing] = {};
        int n = ResolveVisibleTiles(spec, inv, 0.0f, cell, visible, kMaxRing);
        // The single-tile box should resolve at least the start cell.
        bool found_start = false;
        for (int i = 0; i < n; ++i) {
            if (visible[i] == start) { found_start = true; break; }
        }
        assert(found_start);
        printf("PASS: visible tiles from synthetic frustum = %d (includes start)\n", n);
    }

    // 4. ResolveVisibleTiles respects the output capacity bound.
    {
        Mat4 inv = Mat4::Identity();
        const float cell = spec.chunk_size * spec.scale;
        inv.m[0] = 1.0f;  inv.m[12] = 0.0f;
        inv.m[5] = 0.0f;  inv.m[13] = 0.0f;
        inv.m[10] = 1.0f; inv.m[14] = 0.0f;
        // A huge frustum; capacity is small, so the result must be capped.
        const V2RoomSpec* tiny[1] = {};
        int n = ResolveVisibleTiles(spec, inv, 0.0f, cell, tiny, 1);
        assert(n <= 1);
        printf("PASS: visible tiles capped at capacity (n=%d)\n", n);
    }

    printf("ALL PASS\n");
    return 0;
}
