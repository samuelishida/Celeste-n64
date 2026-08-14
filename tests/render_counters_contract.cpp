// Host test for the per-frame render draw counters (Pattern A: header-only,
// no N64 deps). Asserts (Inc 1 / D6 + Inc 2 / instrumentation):
//   (a) `RenderCounters` zero-initializes (all fields 0, incl. the distant
//       pass's own split: distant_batches/distant_vert_loads/distant_syncs);
//   (b) field names match the `RenderCounts` budget semantics (distant_cells,
//       near_batches, texture_uploads, vert_loads, syncs);
//   (c) `BudgetsExceeded` uses the matching budget caps.
//
// Build:
//   g++ -std=c++17 -Isrc/user tests/render_counters_contract.cpp
#include <cstdio>

#include "gameplay/render/open_world_renderer.hpp"
#include "gameplay/render/render_budgets.hpp"

using namespace madeline_cube;

static int failures = 0;

static void expect(bool cond, const char* what) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

int main() {
    // (a) RenderCounters zero-initializes, including the distant-pass split.
    {
        const RenderCounters c;
        expect(c.distant_cells == 0 && c.near_batches == 0 &&
                   c.texture_uploads == 0 && c.vert_loads == 0 && c.syncs == 0,
               "(a) RenderCounters zero-initializes");
        expect(c.distant_batches == 0 && c.distant_vert_loads == 0 &&
                   c.distant_syncs == 0,
               "(a) distant-pass counter split zero-initializes");
    }

    // (b) Field semantics: the counters are uint32_t and match the budget
    // struct's field names (distant_cells, texture_uploads).
    {
        RenderCounters c;
        c.distant_cells = 1;
        c.near_batches = 2;
        c.texture_uploads = 3;
        c.vert_loads = 4;
        c.syncs = 5;
        c.distant_batches = 6;
        c.distant_vert_loads = 7;
        c.distant_syncs = 8;
        expect(c.distant_cells == 1 && c.near_batches == 2 &&
                   c.texture_uploads == 3 && c.vert_loads == 4 && c.syncs == 5,
               "(b) counters hold assigned values");
        expect(c.distant_batches == 6 && c.distant_vert_loads == 7 &&
                   c.distant_syncs == 8,
               "(b) distant split holds assigned values");
    }

    // (c) BudgetsExceeded uses matching caps.
    {
        RenderCounts counts;
        expect(!BudgetsExceeded(counts), "(c) zero counts within budget");
        counts.distant_cells = kMaxDistantCellsPerFrame + 1;
        expect(BudgetsExceeded(counts), "(c) distant_cells over cap trips");
        counts = RenderCounts{};
        counts.texture_uploads = kMaxTextureUploadsPerFrame + 1;
        expect(BudgetsExceeded(counts), "(c) texture_uploads over cap trips");
        counts = RenderCounts{};
        counts.visible_cells = kMaxVisibleCells + 1;
        expect(BudgetsExceeded(counts), "(c) visible_cells over cap trips");
    }

    if (failures == 0) {
        std::printf("render_counters_contract: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "render_counters_contract: %d check(s) failed\n",
                 failures);
    return 1;
}
