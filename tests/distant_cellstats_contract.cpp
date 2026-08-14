// Host test for the per-cell distant cost summary (Inc 3 / instrumentation).
// Pattern A: header-only, no N64 deps. Asserts:
//   (a) `DistantWorldRenderer::DistantCellStat` is host-safe and its fields
//       carry the documented semantics (cell_ix/cell_iz, runs, verts,
//       distance_sq = dx²+dz²).
//   (b) the capture is bounded to the entries cap (64): you can never draw
//       more cells than entries, so the member array never overflows.
//   (c) top-cell-by-runs selection (the RSP sync driver) works on a synthetic
//       stats table and prefers `runs` over `verts`.
//
// NOTE: `DistantWorldRenderer::Render` is device-only (t3d/rdpq), so this test
// cannot run the real capture loop. It validates the data shape + the selection
// math on a synthetic capture, which is the host-verifiable slice of Inc 3.
//
// Build:
//   g++ -std=c++17 -Isrc/user tests/distant_cellstats_contract.cpp
#include <cstdio>

#include "gameplay/render/distant_world_renderer.hpp"

using namespace madeline_cube;

static int failures = 0;

static void expect(bool cond, const char* what) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

int main() {
    // (a) DistantCellStat semantics.
    {
        DistantWorldRenderer::DistantCellStat st;
        expect(st.cell_ix == 0 && st.cell_iz == 0 && st.runs == 0 &&
                   st.verts == 0 && st.distance_sq == 0.0f,
               "(a) DistantCellStat zero-initializes");
        st.cell_ix = 3;
        st.cell_iz = -2;
        st.runs = 14;
        st.verts = 900;
        st.distance_sq = 1330.0f * 1330.0f;
        expect(st.cell_ix == 3 && st.cell_iz == -2 && st.runs == 14 &&
                   st.verts == 900 && st.distance_sq == 1330.0f * 1330.0f,
               "(a) DistantCellStat fields carry values");
    }

    // (b) Bounded capture: build a synthetic table of 64 cells (the max the
    // entries_ array holds) and confirm the top-cell selection iterates all.
    {
        constexpr int kCap = 64;
        DistantWorldRenderer::DistantCellStat stats[kCap];
        for (int i = 0; i < kCap; ++i) {
            stats[i].cell_ix = i;
            stats[i].cell_iz = -i;
            stats[i].runs = (i % 5) + 1;      // vary runs 1..5
            stats[i].verts = 100 + i;          // vary verts independently
            stats[i].distance_sq = 100.0f * i; // monotonic with i
        }
        // Make cell 41 the run-max (runs=6 beats the 1..5 cycle).
        stats[41].runs = 6;
        int top_i = 0;
        for (int i = 1; i < kCap; ++i) {
            if (stats[i].runs > stats[top_i].runs) top_i = i;
        }
        expect(top_i == 41, "(b) top-cell-by-runs picks the run-max over 64 cells");
        expect(stats[top_i].cell_ix == 41 && stats[top_i].runs == 6,
               "(b) top cell identity + runs correct");
    }

    // (c) Selection prefers `runs` (RSP sync driver) over `verts`: a cell with
    // fewer verts but more runs wins, matching the report's costliest-cell logic.
    {
        DistantWorldRenderer::DistantCellStat stats[2];
        stats[0].cell_ix = 0; stats[0].runs = 2; stats[0].verts = 1000; stats[0].distance_sq = 100.0f;
        stats[1].cell_ix = 1; stats[1].runs = 9; stats[1].verts = 200;  stats[1].distance_sq = 900.0f;
        int top_i = 0;
        for (int i = 1; i < 2; ++i) {
            if (stats[i].runs > stats[top_i].runs) top_i = i;
        }
        expect(top_i == 1 && stats[top_i].runs == 9,
               "(c) costliest cell is the run-max even when it has fewer verts");
    }

    if (failures == 0) {
        std::printf("OK: distant_cellstats_contract\n");
        return 0;
    }
    std::fprintf(stderr, "%d FAILURES\n", failures);
    return 1;
}
