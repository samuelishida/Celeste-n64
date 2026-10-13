// Host-side incremental ring-diff test (streaming-memory-opt Inc 1).
//
// Exercises the pure `ResolveRingDiff` helper (inline in `tile_streamer.hpp`)
// against a synthetic `MapSpecV2` grid. Asserts:
//   - a center→neighbor transition classifies the overlap as keep, the new
//     cells as load, and the departed cells as free — load count = the new
//     cells, NOT the full ring (the "no 9× reload" invariant);
//   - every cell is classified exactly once (keep/load/free are mutually
//     exclusive and cover the union of the two rings);
//   - a rapid A→B→A→C sequence never double-frees or leaks a cell at the
//     decision level (a step only frees cells it currently has and only loads
//     cells it lacks; the maintained resident set always equals the ring);
//   - a map-edge center (clipped ring) still keeps the overlap and loads only
//     the new cells (free can be 0);
//   - a 0-overlap "teleport" reloads everything (identical to the old
//     free-all + load-all behavior — no regression).
//
// This is Pattern A (header-only, no renderer linking): it tests the decision
// logic, not the N64 renderer-pointer keep/free (which the on-device smoke
// launch + A→B→A→C crash guard cover).
//
// Note on the plan's "7 keep / 2 load / 2 free" projection: a Chebyshev-1 ring
// shifted by one cell overlaps in 6 cells (a 3x3 block shifted by 1 shares a
// 3x2 = 6 region), so the real 1-step diff is 6 keep / 3 load / 3 free. The
// load-bearing invariant is "load = new cells, not 9," which is what is
// asserted here (R5: document the delta, don't force the projection).
//
// Build:
//   g++ -std=c++17 -Isrc/user tests/tile_streamer_diff_smoke.cpp \
//     -o /tmp/tile_streamer_diff_smoke
// Run:
//   /tmp/tile_streamer_diff_smoke

#include <cassert>
#include <cstdio>
#include <cstring>

#include "gameplay/render/tile_streamer.hpp"

using namespace madeline_cube;

namespace {

// Build a synthetic grid of rooms: ix in [ix0, ix1], iz in [iz0, iz1]. Each
// room gets a unique id and a distinct render_origin. Returns the room count.
int BuildGrid(MapSpecV2& spec, int ix0, int ix1, int iz0, int iz1) {
    spec.room_count = 0;
    for (int iz = iz0; iz <= iz1; ++iz) {
        for (int ix = ix0; ix <= ix1; ++ix) {
            V2RoomSpec& r = spec.rooms[spec.room_count];
            std::snprintf(r.id, sizeof(r.id), "cell_%d_%d", ix, iz);
            r.cell_ix = ix;
            r.cell_iz = iz;
            r.render_origin = {static_cast<float>(ix) * 240.0f, 0.0f,
                               static_cast<float>(iz) * 240.0f};
            std::snprintf(r.lvl_path, sizeof(r.lvl_path),
                          "rom:/lvl/test/%s.lvl", r.id);
            ++spec.room_count;
        }
    }
    return spec.room_count;
}

// Find a room by grid index (avoids id-format assumptions for negatives).
const V2RoomSpec* FindCell(const MapSpecV2& spec, int ix, int iz) {
    for (int i = 0; i < spec.room_count; ++i) {
        if (spec.rooms[i].cell_ix == ix && spec.rooms[i].cell_iz == iz)
            return &spec.rooms[i];
    }
    return nullptr;
}

// Build a ResidentSet from a resolved ring (mirrors what TileStreamer keeps).
ResidentSet MakeSet(const V2RoomSpec* ring[], int count) {
    ResidentSet s;
    s.count = 0;
    for (int i = 0; i < count; ++i) {
        s.spec[s.count] = ring[i];
        s.last_used[s.count] = 0;
        ++s.count;
    }
    return s;
}

// Count how many times a pointer appears in an array.
int CountPtr(const V2RoomSpec* const arr[], int n, const V2RoomSpec* p) {
    int c = 0;
    for (int i = 0; i < n; ++i)
        if (arr[i] == p) ++c;
    return c;
}

// Assert a cell is classified exactly once across keep/load/free.
void AssertExclusive(const RingDiffResult& d, const V2RoomSpec* p) {
    const int k = CountPtr(d.keep, d.keep_count, p);
    const int l = CountPtr(d.load, d.load_count, p);
    const int f = CountPtr(d.free, d.free_count, p);
    assert(k + l + f == 1);
}

// Apply a diff to a resident set: the new set is exactly the new ring (each
// cell kept or loaded). Mirrors what TileStreamer::SetCenter does to its
// arrays. Returns the new resident set.
ResidentSet ApplyDiff(const RingDiffResult& d, const V2RoomSpec* const new_ring[],
                      int new_count) {
    ResidentSet s;
    s.count = 0;
    for (int i = 0; i < new_count; ++i) {
        const V2RoomSpec* p = new_ring[i];
        bool present = false;
        for (int k = 0; k < d.keep_count; ++k)
            if (d.keep[k] == p) present = true;
        for (int k = 0; k < d.load_count; ++k)
            if (d.load[k] == p) present = true;
        assert(present);  // every new-ring cell is kept or loaded
        s.spec[s.count] = p;
        s.last_used[s.count] = 0;
        ++s.count;
    }
    return s;
}

}  // namespace

int main() {
    // A 6×5 grid (ix -2..3, iz -2..2). Large enough for the 3×3 drawn ring
    // plus forward-prioritized Cheb-2 preload cells at A=(0,0) and B=(1,0).
    MapSpecV2 spec;
    const int nrooms = BuildGrid(spec, -2, 3, -2, 2);
    assert(nrooms == 30);
    printf("built %d-room grid\n", nrooms);

    const V2RoomSpec* A = FindCell(spec, 0, 0);
    const V2RoomSpec* B = FindCell(spec, 1, 0);
    const V2RoomSpec* C = FindCell(spec, 2, 0);
    const V2RoomSpec* E = FindCell(spec, -2, 0);  // left-edge center
    assert(A && B && C && E);

    // ── 1. A→B: a 1-step move. The rings overlap in 6 cells; B's ring adds 3
    //    new cells (the ix=2 column) and drops 3 (the ix=-1 column). Assert the
    //    diff classifies exactly that: load = new cells (3), NOT the full ring
    //    (9); keep = overlap (6); free = departed (3).
    {
        const V2RoomSpec* ringA[kMaxRing] = {};
        const int nA = ResolveDistanceRing(spec, *A, ringA, kMaxRing);
        assert(nA == 9);
        ResidentSet setA = MakeSet(ringA, nA);

        const V2RoomSpec* ringB[kMaxRing] = {};
        const int nB = ResolveDistanceRing(spec, *B, ringB, kMaxRing);
        assert(nB == 9);

        const RingDiffResult d = ResolveRingDiff(setA, ringB, nB);
        printf("A->B: keep=%d load=%d free=%d\n", d.keep_count, d.load_count,
               d.free_count);
        assert(d.keep_count == 6);
        assert(d.load_count == 3);  // the new cells, not 9
        assert(d.free_count == 3);  // the departed cells
        // Every cell in the union is classified exactly once.
        for (int i = 0; i < nA; ++i) AssertExclusive(d, ringA[i]);
        for (int i = 0; i < nB; ++i) AssertExclusive(d, ringB[i]);
        // B (the new center) was already resident in A's ring, so it is kept;
        // the 3 loads are exactly the ix=2 column.
        assert(CountPtr(d.keep, d.keep_count, B) == 1);
        assert(CountPtr(d.load, d.load_count, FindCell(spec, 2, -1)) == 1);
        assert(CountPtr(d.load, d.load_count, FindCell(spec, 2, 0)) == 1);
        assert(CountPtr(d.load, d.load_count, FindCell(spec, 2, 1)) == 1);
        printf("PASS: A->B diff (6 keep / 3 load / 3 free)\n");
    }

    // ── 2. A→B→A→C: maintain the resident set across the sequence and assert
    //    no double-free / no leak at the decision level. Each step must only
    //    free cells it currently has and only load cells it lacks; the
    //    maintained set must always equal the current ring.
    {
        const V2RoomSpec* ringA[kMaxRing] = {};
        const int nA = ResolveDistanceRing(spec, *A, ringA, kMaxRing);
        const V2RoomSpec* ringB[kMaxRing] = {};
        const int nB = ResolveDistanceRing(spec, *B, ringB, kMaxRing);
        const V2RoomSpec* ringC[kMaxRing] = {};
        const int nC = ResolveDistanceRing(spec, *C, ringC, kMaxRing);

        ResidentSet cur = MakeSet(ringA, nA);
        const V2RoomSpec* const* rings[3] = {ringB, ringA, ringC};
        const int counts[3] = {nB, nA, nC};

        for (int s = 0; s < 3; ++s) {
            const RingDiffResult d = ResolveRingDiff(cur, rings[s], counts[s]);
            // Only free what we currently have (no double-free).
            for (int i = 0; i < d.free_count; ++i)
                assert(cur.IndexOf(d.free[i]) >= 0);
            // Only load what we lack (no redundant reload).
            for (int i = 0; i < d.load_count; ++i)
                assert(cur.IndexOf(d.load[i]) < 0);
            // Kept cells are resident; keep+load cover the whole new ring.
            for (int i = 0; i < d.keep_count; ++i)
                assert(cur.IndexOf(d.keep[i]) >= 0);
            assert(d.keep_count + d.load_count == counts[s]);
            cur = ApplyDiff(d, rings[s], counts[s]);
            assert(cur.count == counts[s]);
        }
        // Final resident set == C's ring exactly.
        for (int i = 0; i < nC; ++i) assert(cur.IndexOf(ringC[i]) >= 0);
        printf("PASS: A->B->A->C sequence (no double-free / leak)\n");
    }

    // ── 3. Map-edge center: E=(-2,0) has a clipped ring (ix -2..-1, 6 cells).
    //    Moving E→A keeps the overlap (the ix=-1 column, 3 cells) and loads
    //    A's 6 new cells; 3 depart.
    {
        const V2RoomSpec* ringE[kMaxRing] = {};
        const int nE = ResolveDistanceRing(spec, *E, ringE, kMaxRing);
        assert(nE == 6);  // clipped at the left edge
        ResidentSet setE = MakeSet(ringE, nE);

        const V2RoomSpec* ringA[kMaxRing] = {};
        const int nA = ResolveDistanceRing(spec, *A, ringA, kMaxRing);
        assert(nA == 9);

        const RingDiffResult d = ResolveRingDiff(setE, ringA, nA);
        printf("E->A: keep=%d load=%d free=%d\n", d.keep_count, d.load_count,
               d.free_count);
        assert(d.keep_count == 3);
        assert(d.load_count == 6);
        assert(d.free_count == 3);
        for (int i = 0; i < nE; ++i) AssertExclusive(d, ringE[i]);
        for (int i = 0; i < nA; ++i) AssertExclusive(d, ringA[i]);
        printf("PASS: map-edge center E->A (3 keep / 6 load / 3 free)\n");
    }

    // ── 4. 0-overlap "teleport": E=(-2,0) ring (ix -2..-1) and C=(2,0) ring
    //    (ix 1..3) share no cells. Everything reloads — identical to the old
    //    free-all + load-all behavior (no regression).
    {
        const V2RoomSpec* ringE[kMaxRing] = {};
        const int nE = ResolveDistanceRing(spec, *E, ringE, kMaxRing);
        assert(nE == 6);
        ResidentSet setE = MakeSet(ringE, nE);

        const V2RoomSpec* ringC[kMaxRing] = {};
        const int nC = ResolveDistanceRing(spec, *C, ringC, kMaxRing);
        assert(nC == 9);

        const RingDiffResult d = ResolveRingDiff(setE, ringC, nC);
        printf("E->C (teleport): keep=%d load=%d free=%d\n", d.keep_count,
               d.load_count, d.free_count);
        assert(d.keep_count == 0);
        assert(d.load_count == 9);  // all reload
        assert(d.free_count == 6);  // all free
        for (int i = 0; i < nE; ++i) AssertExclusive(d, ringE[i]);
        for (int i = 0; i < nC; ++i) AssertExclusive(d, ringC[i]);
        printf("PASS: 0-overlap teleport E->C (0 keep / 9 load / 6 free)\n");
    }

    // ── 5. Runtime preserves the full 3×3 core, then spends four resident
    //    slots on forward Cheb-2 coverage. All ready residents remain drawable.
    {
        assert(kMaxRing == 9 && kMaxResident == 13);
        const Vec3 camera_forward{1.0f, 0.0f, 0.0f};
        const V2RoomSpec* ringA[kMaxResident] = {};
        const int nA = ResolveForwardWedge(spec, *A, camera_forward, ringA,
                                           kMaxResident, /*max_cheb=*/2);
        assert(nA == 13);
        for (int iz = -1; iz <= 1; ++iz) {
            for (int ix = -1; ix <= 1; ++ix) {
                assert(CountPtr(ringA, nA, FindCell(spec, ix, iz)) == 1);
            }
        }
        assert(CountPtr(ringA, nA, FindCell(spec, 2, 0)) == 1);
        assert(CountPtr(ringA, nA, FindCell(spec, -2, 0)) == 0);
        ResidentSet setA = MakeSet(ringA, nA);

        const V2RoomSpec* ringB[kMaxResident] = {};
        const int nB = ResolveForwardWedge(spec, *B, camera_forward, ringB,
                                           kMaxResident, /*max_cheb=*/2);
        assert(nB == 13);

        const RingDiffResult d = ResolveRingDiff(setA, ringB, nB);
        printf("13-cell A->B: keep=%d load=%d free=%d\n", d.keep_count,
               d.load_count, d.free_count);
        assert(d.keep_count + d.load_count == nB);
        assert(d.keep_count + d.free_count == nA);
        assert(d.load_count == d.free_count);
        for (int i = 0; i < nA; ++i) AssertExclusive(d, ringA[i]);
        for (int i = 0; i < nB; ++i) AssertExclusive(d, ringB[i]);
        printf("PASS: bounded 13-cell forward residency diff\n");
    }

    printf("tile_streamer_diff_smoke: all assertions passed\n");
    return 0;
}
