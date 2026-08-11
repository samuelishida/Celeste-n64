// Host-side tile-streamer LRU eviction contract test (Inc 3).
//
// Exercises the `ResidentSet` inline bookkeeping (IndexOf / Touch /
// EvictCandidate / OverCapacity) that the device `TileStreamer` uses to keep
// its bounded resident pool within capacity and never evict the center.
// Host-safe — links `mappack_loader.cpp` only (Pattern C).
//
// Build:
//   g++ -std=c++17 -Isrc/user tests/tile_stream_lru_contract.cpp \
//     src/user/gameplay/world/mappack_loader.cpp \
//     -o /tmp/tile_stream_lru_contract
// Run:
//   /tmp/tile_stream_lru_contract

#include <cassert>
#include <cstdio>
#include <cstring>

#include "gameplay/render/tile_streamer.hpp"

using namespace madeline_cube;

int main() {
    // A small mock manifest so we have distinct V2RoomSpec objects.
    MapSpecV2 spec;
    spec.room_count = 5;
    for (int i = 0; i < 5; ++i) {
        std::snprintf(spec.rooms[i].id, V2RoomSpec::kIdLen, "cell_%d_0", i);
        spec.rooms[i].cell_ix = i;
        spec.rooms[i].cell_iz = 0;
    }
    const V2RoomSpec* center = &spec.rooms[0];
    const V2RoomSpec* a = &spec.rooms[1];
    const V2RoomSpec* b = &spec.rooms[2];
    const V2RoomSpec* c = &spec.rooms[3];
    const V2RoomSpec* d = &spec.rooms[4];

    ResidentSet set;
    assert(set.count == 0);
    assert(set.capacity == kMaxRing);

    // Populate: center + 4 residents, all "used" at distinct frames.
    set.spec[set.count++] = center;
    set.spec[set.count++] = a;
    set.spec[set.count++] = b;
    set.spec[set.count++] = c;
    set.spec[set.count++] = d;
    set.last_used[0] = 10;  // center used long ago
    set.last_used[1] = 20;  // a
    set.last_used[2] = 30;  // b
    set.last_used[3] = 40;  // c
    set.last_used[4] = 50;  // d (most recently used)
    assert(set.count == 5);

    // IndexOf.
    assert(set.IndexOf(center) == 0);
    assert(set.IndexOf(d) == 4);
    assert(set.IndexOf(&spec.rooms[3]) == 3);

    // EvictCandidate never picks the center, even when it is the LRU.
    {
        const int victim = set.EvictCandidate(center);
        assert(victim != 0);        // never the center
        assert(victim == 1);        // 'a' has the smallest non-center frame
        printf("PASS: LRU evicts non-center (victim=%d)\n", victim);
    }

    // Touch updates last_used so a touched resident is no longer the LRU.
    set.Touch(a, 100);
    {
        const int victim = set.EvictCandidate(center);
        assert(victim == 2);        // now 'b' is the LRU non-center
        printf("PASS: Touch promotes a resident out of LRU (victim=%d)\n", victim);
    }

    // Evict the LRU non-center manually (simulating the device compaction).
    {
        const int victim = set.EvictCandidate(center);
        assert(victim == 2);
        // Compact.
        for (int k = victim; k < set.count - 1; ++k) {
            set.spec[k] = set.spec[k + 1];
            set.last_used[k] = set.last_used[k + 1];
        }
        --set.count;
        assert(set.IndexOf(b) == -1);  // evicted
        assert(set.IndexOf(center) == 0);
        printf("PASS: eviction removes the LRU non-center; center preserved\n");
    }

    // OverCapacity reflects count > capacity.
    {
        ResidentSet full;
        full.capacity = 1;
        full.spec[full.count++] = center;
        assert(!full.OverCapacity());
        full.spec[full.count++] = a;
        assert(full.OverCapacity());
        // EvictCandidate with all-center-and-one-extra still skips the center.
        const int victim = full.EvictCandidate(center);
        assert(victim == 1);
        printf("PASS: OverCapacity + single non-center evictable\n");
    }

    // Edge: a set whose only resident is the center has nothing evictable.
    {
        ResidentSet only_center;
        only_center.spec[only_center.count++] = center;
        assert(only_center.EvictCandidate(center) == -1);
        printf("PASS: center-only set has no evictable candidate\n");
    }

    printf("ALL PASS\n");
    return 0;
}
