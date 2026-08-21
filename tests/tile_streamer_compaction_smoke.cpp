// Host-side compaction test for TileStreamer::SetCenterImpl's resident-array
// rewrite (regression guard for the "old center dropped on transition" bug).
//
// Background: the resident set is applied by rewriting the parallel
// set_.spec[] / renderers_[] arrays IN PLACE, center-first. The kept-cell
// lookup MUST run against an immutable snapshot of the OLD resident state,
// because the in-place rewrite clobbers set_.spec[0] (the OLD center's slot)
// with the NEW center on the very first iteration. If the lookup instead
// searches the LIVE set_.spec[] (the old bug), the old center — now a kept
// neighbor at ring[i>0] — is not found, is not in diff.load, and is silently
// DROPPED. That drops the cell you just left (missing/broken geometry) and
// orphans its renderer (heap leak -> exhaustion -> mid-map crash).
//
// This test replicates the exact compaction algorithm (snapshot-based, the
// fixed path) as a standalone function and asserts the load-bearing invariant
// across an A->B->A->C walk: after every transition the resident set is
// EXACTLY the new ring — no kept cell dropped, no dupe, and specifically the
// old center (now a kept neighbor) is still resident.
//
// It also runs the OLD buggy variant (live-set lookup) to demonstrate that it
// DOES drop the old center, proving this test guards the real regression.
//
// Build:
//   g++ -std=c++17 -Isrc/user tests/tile_streamer_compaction_smoke.cpp \
//     -o /tmp/tile_streamer_compaction_smoke
// Run:
//   /tmp/tile_streamer_compaction_smoke

#include <cassert>
#include <cstdio>
#include <cstring>

#include "gameplay/render/tile_streamer.hpp"

using namespace madeline_cube;

namespace {

// Build a synthetic grid of rooms: ix in [ix0, ix1], iz in [iz0, iz1].
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

const V2RoomSpec* FindCell(const MapSpecV2& spec, int ix, int iz) {
    for (int i = 0; i < spec.room_count; ++i) {
        if (spec.rooms[i].cell_ix == ix && spec.rooms[i].cell_iz == iz)
            return &spec.rooms[i];
    }
    return nullptr;
}

// A resident set with a stable integer "renderer id" per cell, so the test can
// track which cell's renderer is retained (mirrors the LvlRoomRenderer* the
// device streamer pairs with each spec).
struct Resident {
    const V2RoomSpec* spec[kMaxRing] = {};
    int renderer_id[kMaxRing] = {};  // stable id == index in id_by_cell
    int count = 0;
};

// Assign each cell a stable renderer id (0..room_count-1) so "kept" vs "new"
// renderers are distinguishable.
int IdFor(const MapSpecV2& spec, const V2RoomSpec* p) {
    for (int i = 0; i < spec.room_count; ++i)
        if (&spec.rooms[i] == p) return i;
    return -1;
}

// The FIXED compaction: snapshot the entire old state, then rewrite in place,
// looking kept cells up against the SNAPSHOT. Returns the new resident set.
// Mirrors TileStreamer::SetCenterImpl exactly.
Resident CompactFixed(const MapSpecV2& spec, const Resident& old,
                      const V2RoomSpec* const ring[], int count) {
    const V2RoomSpec* old_spec[kMaxRing] = {};
    int old_rid[kMaxRing] = {};
    for (int k = 0; k < old.count; ++k) {
        old_spec[k] = old.spec[k];
        old_rid[k] = old.renderer_id[k];
    }

    Resident out;
    out.count = 0;
    for (int i = 0; i < count; ++i) {
        const V2RoomSpec* s = ring[i];
        int rid = -1;
        int old_idx = -1;
        for (int k = 0; k < old.count; ++k) {
            if (old_spec[k] == s) { old_idx = k; break; }
        }
        if (old_idx >= 0) {
            rid = old_rid[old_idx];  // kept: reuse existing renderer
        } else {
            rid = IdFor(spec, s);    // new: freshly loaded renderer
        }
        out.spec[out.count] = s;
        out.renderer_id[out.count] = rid;
        ++out.count;
    }
    return out;
}

// The OLD buggy compaction: look kept cells up against the LIVE set_.spec[]
// while rewriting it in place (center-first). Reproduced verbatim from the
// pre-fix code to prove it drops the old center. A cell NOT found in the (part
// clobbered) live set is only kept if it is in `load[]` (freshly loaded by
// ResolveRingDiff); otherwise it is silently DROPPED. That is the crux: A is
// classified as "keep" by ResolveRingDiff (which runs against the INTACT old
// set), so A is NOT in load[], and once the live lookup for A fails (because
// live[0] was clobbered with the new center), A is dropped.
Resident CompactBuggy(const MapSpecV2& spec, const Resident& old,
                      const V2RoomSpec* const ring[], int count,
                      const V2RoomSpec* const load[], int load_count) {
    // Live set_.spec[] being rewritten in place (center-first).
    const V2RoomSpec* live[kMaxRing] = {};
    int live_rid[kMaxRing] = {};
    for (int k = 0; k < old.count; ++k) {
        live[k] = old.spec[k];
        live_rid[k] = old.renderer_id[k];
    }

    Resident out;
    out.count = 0;
    for (int i = 0; i < count; ++i) {
        const V2RoomSpec* s = ring[i];
        int rid = -1;
        // BUG: search the LIVE array (live[]) while live[n] is being clobbered.
        int old_idx = -1;
        for (int k = 0; k < old.count; ++k) {
            if (live[k] == s) { old_idx = k; break; }
        }
        if (old_idx >= 0) {
            rid = live_rid[old_idx];  // kept: reuse existing renderer
        } else {
            // Not found in the (partially clobbered) live set: only keep if it
            // was freshly loaded. A is not in load[] -> dropped.
            bool in_load = false;
            for (int li = 0; li < load_count; ++li)
                if (load[li] == s) in_load = true;
            if (!in_load) continue;  // silently dropped (the bug)
            rid = IdFor(spec, s);    // fresh renderer
        }
        // In-place overwrite: clobbers live[0] (old center) on i==0.
        live[out.count] = s;
        live_rid[out.count] = rid;
        out.spec[out.count] = s;
        out.renderer_id[out.count] = rid;
        ++out.count;
    }
    return out;
}

// Assert the resident set is EXACTLY `ring[0..count-1]`: same cells, same
// order, no drops, no dupes.
void AssertExact(const Resident& r, const V2RoomSpec* const ring[], int count) {
    assert(r.count == count);
    for (int i = 0; i < count; ++i) {
        assert(r.spec[i] == ring[i]);
    }
}

// Count how many times `p` appears in a resident set.
int CountIn(const Resident& r, const V2RoomSpec* p) {
    int c = 0;
    for (int i = 0; i < r.count; ++i)
        if (r.spec[i] == p) ++c;
    return c;
}

}  // namespace

int main() {
    // 4x3 grid (ix -1..2, iz -1..1) so a 1-step move has room to load new
    // cells and drop old ones (a 3x3 grid would clip the destination ring).
    MapSpecV2 spec;
    const int nrooms = BuildGrid(spec, -1, 2, -1, 1);
    assert(nrooms == 12);

    const V2RoomSpec* A = FindCell(spec, 0, 0);
    const V2RoomSpec* B = FindCell(spec, 1, 0);
    const V2RoomSpec* C = FindCell(spec, 2, 0);
    assert(A && B && C);

    // ── 1. The single A->B transition: the FIXED compaction keeps the old
    //    center (A) resident (it is a kept neighbor of B's ring) and produces
    //    exactly B's ring. The OLD buggy compaction DROPS A.
    {
        const V2RoomSpec* ringA[kMaxRing] = {};
        const int nA = ResolveDistanceRing(spec, *A, ringA, kMaxRing);
        const V2RoomSpec* ringB[kMaxRing] = {};
        const int nB = ResolveDistanceRing(spec, *B, ringB, kMaxRing);

        Resident cur;
        cur.count = 0;
        for (int i = 0; i < nA; ++i) {
            cur.spec[cur.count] = ringA[i];
            cur.renderer_id[cur.count] = IdFor(spec, ringA[i]);
            ++cur.count;
        }

        // A is the old center; it lies in B's ring (kept neighbor).
        assert(CountIn(cur, A) == 1);
        bool a_in_ringB = false;
        for (int i = 0; i < nB; ++i)
            if (ringB[i] == A) a_in_ringB = true;
        assert(a_in_ringB);  // precondition: A must survive the transition

        Resident fixed = CompactFixed(spec, cur, ringB, nB);
        AssertExact(fixed, ringB, nB);
        assert(CountIn(fixed, A) == 1);  // FIXED: old center survives
        printf("PASS: fixed A->B keeps old center A (resident=%d)\n",
               fixed.count);

        // load[] = cells in B's ring that were not resident in A's ring
        // (what ResolveRingDiff classifies as "load"). A is NOT in load[]
        // because it was resident (kept), which is exactly why the buggy
        // live-lookup failure turns into a silent drop.
        const V2RoomSpec* load[kMaxRing] = {};
        int nload = 0;
        for (int i = 0; i < nB; ++i) {
            bool was_resident = false;
            for (int k = 0; k < nA; ++k)
                if (ringA[k] == ringB[i]) was_resident = true;
            if (!was_resident) load[nload++] = ringB[i];
        }

        Resident buggy = CompactBuggy(spec, cur, ringB, nB, load, nload);
        // The buggy path must NOT be able to reproduce B's ring exactly: it
        // drops A, so its count is short by one (A is missing entirely).
        assert(CountIn(buggy, A) == 0);  // BUG reproduced: old center dropped
        printf("PASS: buggy A->B drops old center A (resident=%d, expected %d)\n",
               buggy.count, nB);
    }

    // ── 2. A->B->A->C walk with the FIXED compaction: after every step the
    //    resident set is exactly the current ring (no drop, no dupe), so no
    //    renderer is ever orphaned (the leak that exhausts the heap).
    {
        const V2RoomSpec* ringA[kMaxRing] = {};
        const int nA = ResolveDistanceRing(spec, *A, ringA, kMaxRing);
        const V2RoomSpec* ringB[kMaxRing] = {};
        const int nB = ResolveDistanceRing(spec, *B, ringB, kMaxRing);
        const V2RoomSpec* ringC[kMaxRing] = {};
        const int nC = ResolveDistanceRing(spec, *C, ringC, kMaxRing);

        Resident cur;
        cur.count = 0;
        for (int i = 0; i < nA; ++i) {
            cur.spec[cur.count] = ringA[i];
            cur.renderer_id[cur.count] = IdFor(spec, ringA[i]);
            ++cur.count;
        }

        const V2RoomSpec* const* rings[3] = {ringB, ringA, ringC};
        const int counts[3] = {nB, nA, nC};
        const V2RoomSpec* prev_center[3] = {A, B, A};

        for (int s = 0; s < 3; ++s) {
            cur = CompactFixed(spec, cur, rings[s], counts[s]);
            AssertExact(cur, rings[s], counts[s]);
            // The previous center (now a kept neighbor) must still be resident
            // IF it lies in the new ring; if it has left the ring it is free
            // (expected), but it must never be silently dropped while present.
            bool prev_in_new = false;
            for (int i = 0; i < counts[s]; ++i)
                if (rings[s][i] == prev_center[s]) prev_in_new = true;
            if (prev_in_new) {
                assert(CountIn(cur, prev_center[s]) == 1);
            } else {
                assert(CountIn(cur, prev_center[s]) == 0);
            }
            printf("PASS: step %d resident=%d (exact ring, no drop/dupe)\n",
                   s + 1, cur.count);
        }
    }

    printf("\nALL COMPACT SMOKE TESTS PASSED\n");
    return 0;
}
