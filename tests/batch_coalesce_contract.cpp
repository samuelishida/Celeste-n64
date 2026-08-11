// Host test for material-run batch coalescing (Pattern A: header-only, no N64
// deps). Asserts (Inc 3 / D3 + MUST-FIX #1/#2):
//   (a) consecutive same-material faces coalesce into 1 run (span ≤ 70);
//   (b) the 70-vertex span cap splits a long run into 2 runs;
//   (c) different-material neighbors stay separate;
//   (d) GEOMETRY EQUIVALENCE — replaying the runs with per-face fan origins
//       emits the exact same triangle index set as the uncoalesced faces
//       (a run-wide fan would cross face boundaries and render garbage);
//   (e) first/last face preserved; degenerate faces (vc<3) don't break
//       adjacency; capacity failure returns -1 (caller falls back).
//
// Build:
//   g++ -std=c++17 -Isrc/user tests/batch_coalesce_contract.cpp
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

// Compute the triangle index set for the uncoalesced face list: each face fans
// from its own first_vertex (fv, fv+t+1, fv+t+2). Returns sorted triangles.
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
// origin is (run.first_vertex & ~1) + offset + (run.first_vertex & 1) =
// run.first_vertex + offset. Returns the same sorted triangle set so it can be
// compared with TrianglesUncoalesced.
static std::vector<std::array<uint32_t, 3>> TrianglesCoalesced(
    const FaceSpec* /*faces*/, const BatchRun* runs, int run_count,
    const RunFace* run_faces) {
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
    BatchRun runs[16] = {};
    RunFace run_faces[16] = {};

    // (a) 3 consecutive same-material faces → 1 run, span ≤ 70.
    {
        const FaceSpec faces[3] = {
            {0, 4, 2, 0}, {4, 4, 2, 0}, {8, 4, 2, 0},
        };
        const int n = CoalesceBatches(faces, 3, runs, 16, run_faces, 16, 70);
        expect(n == 1, "(a) 3 same-material faces coalesce to 1 run");
        if (n == 1) {
            expect(runs[0].face_count == 3, "(a) run holds all 3 faces");
            expect(runs[0].vertex_count <= 70, "(a) run span within cap");
            expect(runs[0].vertex_count == 12, "(a) run span is 12");
            expect(runs[0].first_face == 0, "(a) run starts at out_faces 0");
            expect(run_faces[0].offset == 0 && run_faces[1].offset == 4 &&
                       run_faces[2].offset == 8,
                   "(a) face offsets relative to run start");
        }
    }

    // (b) 70-vertex span cap splits a long run into 2 runs.
    {
        const FaceSpec faces[3] = {
            {0, 30, 28, 0}, {30, 30, 28, 0}, {60, 30, 28, 0},
        };
        const int n = CoalesceBatches(faces, 3, runs, 16, run_faces, 16, 70);
        expect(n == 2, "(b) span cap splits into 2 runs");
        if (n == 2) {
            expect(runs[0].face_count == 2 && runs[0].vertex_count == 60,
                   "(b) run 0 holds first 2 faces (span 60)");
            expect(runs[1].face_count == 1 && runs[1].vertex_count == 30,
                   "(b) run 1 holds last face (span 30)");
        }
    }

    // (c) different-material neighbors stay separate.
    {
        const FaceSpec faces[3] = {
            {0, 4, 2, 0}, {4, 4, 2, 1}, {8, 4, 2, 0},
        };
        const int n = CoalesceBatches(faces, 3, runs, 16, run_faces, 16, 70);
        expect(n == 3, "(c) material changes produce 3 separate runs");
    }

    // (d) geometry equivalence across mixed runs + an odd first_vertex.
    {
        const FaceSpec faces[4] = {
            {0, 5, 3, 0},  // f0: vc 5 (odd) — fan (0,1,2)(0,2,3)(0,3,4)
            {5, 4, 2, 0},  // f1: fan (5,6,7)(5,7,8)
            {9, 6, 4, 1},  // f2: mat 1, fan (9..14)
            {15, 4, 2, 1}, // f3: fan (15,16,17)(15,17,18)
        };
        const int n = CoalesceBatches(faces, 4, runs, 16, run_faces, 16, 70);
        expect(n == 2, "(d) two runs (one per material)");
        const auto want = TrianglesUncoalesced(faces, 4);
        const auto got = TrianglesCoalesced(faces, runs, n, run_faces);
        expect(want == got, "(d) coalesced runs emit the SAME triangles");
        if (want != got) {
            std::fprintf(stderr, "  want %zu tris, got %zu tris\n",
                         want.size(), got.size());
        }
    }

    // (e) first/last face preserved + degenerate faces don't break adjacency.
    {
        const FaceSpec faces[4] = {
            {0, 4, 2, 0}, {4, 2, 0, 0},   // degenerate (vc 2) mid-run
            {6, 4, 2, 0}, {10, 4, 2, 0},
        };
        const int n = CoalesceBatches(faces, 4, runs, 16, run_faces, 16, 70);
        expect(n == 1, "(e) degenerate face does not break adjacency");
        if (n == 1) {
            expect(runs[0].face_count == 3, "(e) degenerate face not counted");
            // First face preserved (offset 0) and last face preserved (offset
            // 10 = first_vertex of the last non-degenerate face).
            expect(run_faces[runs[0].first_face].offset == 0,
                   "(e) first face preserved at run start");
            const RunFace& last =
                run_faces[runs[0].first_face + runs[0].face_count - 1];
            expect(last.offset == 10, "(e) last face preserved");
        }
    }

    // Odd first_vertex alignment: run starting at vertex 1 (odd) keeps each
    // face's fan origin exact.
    {
        const FaceSpec faces[2] = {
            {1, 4, 2, 0}, {5, 4, 2, 0},
        };
        const int n = CoalesceBatches(faces, 2, runs, 16, run_faces, 16, 70);
        expect(n == 1, "odd first_vertex still coalesces to 1 run");
        if (n == 1) {
            expect(runs[0].first_vertex == 1 && runs[0].vertex_count == 8,
                   "odd run keeps absolute first_vertex + span");
            // A single t3d_vert_load must cover align(1) + span(8) = 9 → ≤ 70.
            expect((runs[0].first_vertex & 1u) + runs[0].vertex_count <= 70,
                   "aligned load span within RSP cap");
            const auto want = TrianglesUncoalesced(faces, 2);
            const auto got = TrianglesCoalesced(faces, runs, n, run_faces);
            expect(want == got, "odd run emits same triangles");
        }
    }

    // Capacity failure → -1 (caller falls back to per-face batches).
    {
        const FaceSpec faces[3] = {
            {0, 4, 2, 0}, {4, 4, 2, 1}, {8, 4, 2, 2},  // 3 materials → 3 runs
        };
        expect(CoalesceBatches(faces, 3, runs, 2, run_faces, 16, 70) == -1,
               "out_cap exhausted returns -1");
        expect(CoalesceBatches(faces, 3, runs, 16, run_faces, 2, 70) == -1,
               "face_cap exhausted returns -1");
        expect(CoalesceBatches(faces, 3, nullptr, 16, run_faces, 16, 70) == -1,
               "null runs returns -1");
        expect(CoalesceBatches(faces, 3, runs, 16, nullptr, 16, 70) == -1,
               "null run_faces returns -1");
        expect(CoalesceBatches(nullptr, 3, runs, 16, run_faces, 16, 70) == -1,
               "null faces returns -1");
    }

    if (failures == 0) {
        std::printf("batch_coalesce_contract: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "batch_coalesce_contract: %d failures\n", failures);
    return 1;
}
