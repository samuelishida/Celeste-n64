// Host test for the near-pass material sort (Pattern A: header-only, no N64
// deps). Asserts (Inc 3 / D2 + review MUST-FIX):
//   (a) faces reorder so all same-material faces are contiguous;
//   (b) the sort is STABLE (relative order within a material preserved);
//   (c) GEOMETRY EQUIVALENCE — the sorted+coalesced runs emit the exact same
//       triangle index set as the unsorted face list (host-safe: replay each
//       face's fan on paper as (base, base+t+1, base+t+2) index triples, no
//       T3D);
//   (d) capacity failure returns -1 (caller falls back to unsorted).
//
// Build:
//   g++ -std=c++17 -Isrc/user tests/material_sort_contract.cpp
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
    // (a) + (b) + (c): interleaved materials sort into contiguous groups,
    // stable within a material, and emit the same triangles.
    {
        // Interleaved: mat 0, mat 1, mat 0, mat 1 (adjacent coalescing would
        // produce 4 runs; material sort should collapse to 2 groups).
        const FaceSpec faces[4] = {
            {0, 4, 2, 0},   // mat 0
            {4, 4, 2, 1},   // mat 1
            {8, 4, 2, 0},   // mat 0
            {12, 4, 2, 1},  // mat 1
        };
        uint16_t order[4] = {};
        const int groups = SortFacesByMaterial(faces, 4, order, 4);
        expect(groups == 2, "(a) two distinct material groups");

        // (a) all mat-0 faces come before all mat-1 faces in the permutation.
        bool saw_mat1 = false;
        bool contiguous = true;
        for (int i = 0; i < 4; ++i) {
            const uint16_t mat = faces[order[i]].material_id;
            if (mat == 1) saw_mat1 = true;
            if (saw_mat1 && mat == 0) contiguous = false;  // mat 0 after mat 1
        }
        expect(contiguous, "(a) faces contiguous by material");

        // (b) stable: within mat 0, face 0 (idx 0) before face 2 (idx 2);
        // within mat 1, face 1 (idx 1) before face 3 (idx 3).
        int pos0 = -1, pos2 = -1, pos1 = -1, pos3 = -1;
        for (int i = 0; i < 4; ++i) {
            if (order[i] == 0) pos0 = i;
            if (order[i] == 2) pos2 = i;
            if (order[i] == 1) pos1 = i;
            if (order[i] == 3) pos3 = i;
        }
        expect(pos0 >= 0 && pos2 >= 0 && pos0 < pos2,
               "(b) stable: mat-0 face 0 before face 2");
        expect(pos1 >= 0 && pos3 >= 0 && pos1 < pos3,
               "(b) stable: mat-1 face 1 before face 3");

        // (c) geometry equivalence: sort + coalesce, then compare triangles.
        FaceSpec sorted[4] = {};
        for (int i = 0; i < 4; ++i) sorted[i] = faces[order[i]];
        BatchRun runs[8] = {};
        RunFace run_faces[8] = {};
        const int n = CoalesceBatches(sorted, 4, runs, 8, run_faces, 8, 70);
        expect(n == 2, "(c) sorted faces coalesce into 2 runs");
        const auto want = TrianglesUncoalesced(faces, 4);
        const auto got = TrianglesCoalesced(runs, n, run_faces);
        expect(want == got, "(c) sorted+coalesced emits the SAME triangles");
        if (want != got) {
            std::fprintf(stderr, "  want %zu tris, got %zu tris\n",
                         want.size(), got.size());
        }
    }

    // (d) capacity failure returns -1.
    {
        const FaceSpec faces[3] = {
            {0, 4, 2, 0}, {4, 4, 2, 1}, {8, 4, 2, 2},
        };
        uint16_t order[2] = {};
        expect(SortFacesByMaterial(faces, 3, order, 2) == -1,
               "(d) out_capacity < n returns -1");
        expect(SortFacesByMaterial(faces, 3, nullptr, 3) == 0,
               "(d) null out_order returns 0");
        expect(SortFacesByMaterial(nullptr, 3, order, 3) == 0,
               "(d) null src returns 0");
        expect(SortFacesByMaterial(faces, 0, order, 3) == 0,
               "(d) n<=0 returns 0");
    }

    if (failures == 0) {
        std::printf("material_sort_contract: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "material_sort_contract: %d check(s) failed\n",
                 failures);
    return 1;
}
