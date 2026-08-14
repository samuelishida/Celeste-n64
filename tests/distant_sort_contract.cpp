// Host test for the distant-pass material sort (Pattern A: header-only, no
// N64 deps). Asserts (Inc 1 / D1 of the distant-pass perf plan):
//   (a) on an interleaved FaceSpec list (materials alternating, simulating the
//       bake's arbitrary order), SortFacesByMaterial + CoalesceBatches
//       produces FEWER OR EQUAL runs than adjacent-only CoalesceBatches;
//   (b) the sorted+coalesced runs emit the EXACT same triangle set as the
//       unsorted face list (geometry equivalence — the sort only regroups,
//       never changes geometry);
//   (c) the run-count reduction is strictly positive on a worst-case
//       alternating list (every face a different run adjacent-only, collapsing
//       to ~distinct-materials runs after sort).
//
// This mirrors the measured distant-pass win: 1015 → 303 runs across all 45
// baked cells (~22.6 → ~6.7 runs/cell, ~3.3×).
//
// Build:
//   g++ -std=c++17 -Isrc/user tests/distant_sort_contract.cpp
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "gameplay/render/batch_coalesce.hpp"

using namespace madeline_cube;

static int failures = 0;

static void expect(bool cond, const char* what) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

// Compute the triangle index set for a face list: each face fans from its own
// first_vertex (fv, fv+t+1, fv+t+2). Returns sorted triangles.
static std::vector<std::array<uint32_t, 3>> TrianglesUncoalesced(
    const FaceSpec* faces, int n) {
    std::vector<std::array<uint32_t, 3>> tris;
    for (int i = 0; i < n; ++i) {
        if (faces[i].vertex_count < 3 || faces[i].tri_count == 0) continue;
        const uint32_t fv = faces[i].first_vertex;
        for (uint32_t t = 0; t < faces[i].tri_count; ++t) {
            std::array<uint32_t, 3> tri = {fv, fv + t + 1, fv + t + 2};
            std::sort(tri.begin(), tri.end());
            tris.push_back(tri);
        }
    }
    std::sort(tris.begin(), tris.end());
    return tris;
}

// Replay the coalesced runs with per-face fan origins. Each face's absolute
// origin is run.first_vertex + offset. Returns the same sorted triangle set.
static std::vector<std::array<uint32_t, 3>> TrianglesCoalesced(
    const BatchRun* runs, int run_count, const RunFace* run_faces) {
    std::vector<std::array<uint32_t, 3>> tris;
    for (int r = 0; r < run_count; ++r) {
        const BatchRun& run = runs[r];
        for (uint32_t f = 0; f < run.face_count; ++f) {
            const RunFace& rf = run_faces[run.first_face + f];
            const uint32_t origin = run.first_vertex + rf.offset;
            for (uint32_t t = 0; t < rf.tri_count; ++t) {
                std::array<uint32_t, 3> tri = {origin, origin + t + 1,
                                               origin + t + 2};
                std::sort(tri.begin(), tri.end());
                tris.push_back(tri);
            }
        }
    }
    std::sort(tris.begin(), tris.end());
    return tris;
}

int main() {
    // (a) + (b) + (c): interleaved materials — adjacent-only coalescing
    // produces one run per face; material sort collapses to distinct-material
    // groups, with identical geometry.
    {
        // 8 faces, materials alternating 0,1,2,0,1,2,0,1. Adjacent-only
        // coalescing yields 8 runs (every face a different material than its
        // neighbor). Material sort should collapse to 3 groups.
        const FaceSpec faces[8] = {
            {0, 4, 2, 0},   // mat 0
            {4, 4, 2, 1},   // mat 1
            {8, 4, 2, 2},   // mat 2
            {12, 4, 2, 0},  // mat 0
            {16, 4, 2, 1},  // mat 1
            {20, 4, 2, 2},  // mat 2
            {24, 4, 2, 0},  // mat 0
            {28, 4, 2, 1},  // mat 1
        };
        const int n = 8;

        // (a) adjacent-only coalescing (the pre-Inc-1 distant-pass behavior).
        BatchRun adj_runs[16] = {};
        RunFace adj_faces[16] = {};
        const int adj_count = CoalesceBatches(faces, n, adj_runs, 16,
                                              adj_faces, 16, 70);

        // (b) material sort + coalesce (the Inc-1 distant-pass behavior).
        uint16_t order[8] = {};
        const int groups = SortFacesByMaterial(faces, n, order, n);
        FaceSpec sorted[8] = {};
        for (int i = 0; i < n; ++i) sorted[i] = faces[order[i]];
        BatchRun sorted_runs[16] = {};
        RunFace sorted_faces[16] = {};
        const int sorted_count = CoalesceBatches(sorted, n, sorted_runs, 16,
                                                 sorted_faces, 16, 70);

        // (a) sorted produces ≤ runs than adjacent-only.
        expect(sorted_count <= adj_count,
               "(a) sorted run count <= adjacent-only run count");
        // (c) on this worst-case alternating list the reduction is strict.
        expect(sorted_count < adj_count,
               "(c) sorted run count strictly less on alternating list");
        expect(groups == 3, "(c) three distinct material groups");
        expect(adj_count == 8, "(c) adjacent-only yields 8 runs (one per face)");
        expect(sorted_count == 3,
               "(c) sorted yields 3 runs (one per material group)");

        // (b) geometry equivalence: sorted+coalesced emits the SAME triangles
        // as the unsorted face list.
        const auto want = TrianglesUncoalesced(faces, n);
        const auto got = TrianglesCoalesced(sorted_runs, sorted_count,
                                            sorted_faces);
        expect(want == got, "(b) sorted+coalesced emits the SAME triangles");
        if (want != got) {
            std::fprintf(stderr, "  want %zu tris, got %zu tris\n",
                         want.size(), got.size());
        }
    }

    // (a) degenerate faces (vertex_count < 3) are skipped by both paths and
    // do not break the sorted run-count invariant.
    {
        const FaceSpec faces[5] = {
            {0, 4, 2, 0},   // mat 0
            {4, 2, 0, 1},   // degenerate (mat 1)
            {6, 4, 2, 0},   // mat 0
            {10, 4, 2, 1},  // mat 1
            {14, 4, 2, 1},  // mat 1
        };
        const int n = 5;
        BatchRun adj_runs[16] = {};
        RunFace adj_faces[16] = {};
        const int adj_count = CoalesceBatches(faces, n, adj_runs, 16,
                                              adj_faces, 16, 70);

        uint16_t order[5] = {};
        const int groups = SortFacesByMaterial(faces, n, order, n);
        FaceSpec sorted[5] = {};
        for (int i = 0; i < n; ++i) sorted[i] = faces[order[i]];
        BatchRun sorted_runs[16] = {};
        RunFace sorted_faces[16] = {};
        const int sorted_count = CoalesceBatches(sorted, n, sorted_runs, 16,
                                                 sorted_faces, 16, 70);

        expect(sorted_count <= adj_count,
               "(a) degenerate faces keep sorted <= adjacent invariant");
        const auto want = TrianglesUncoalesced(faces, n);
        const auto got = TrianglesCoalesced(sorted_runs, sorted_count,
                                            sorted_faces);
        expect(want == got, "(b) degenerate faces keep geometry equivalence");
    }

    if (failures == 0) {
        std::printf("distant_sort_contract: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "distant_sort_contract: %d check(s) failed\n",
                 failures);
    return 1;
}
