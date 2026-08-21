// Host test for the per-frame render draw counters (Pattern A: header-only,
// no N64 deps). Asserts (Inc 1 / D6):
//   (a) `RenderCounters` zero-initializes (all fields 0);
//   (b) field names match the `RenderCounts` budget semantics (near_batches,
//       texture_uploads, vert_loads, syncs);
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
    // (a) RenderCounters zero-initializes.
    {
        const RenderCounters c;
        expect(c.near_batches == 0 && c.texture_uploads == 0 &&
                   c.vert_loads == 0 && c.syncs == 0,
               "(a) RenderCounters zero-initializes");
    }

    // (b) Field semantics: the counters are uint32_t and match the budget
    // struct's field names (texture_uploads).
    {
        RenderCounters c;
        c.near_batches = 2;
        c.texture_uploads = 3;
        c.vert_loads = 4;
        c.syncs = 5;
        expect(c.near_batches == 2 && c.texture_uploads == 3 &&
                   c.vert_loads == 4 && c.syncs == 5,
               "(b) counters hold assigned values");
    }

    // (c) BudgetsExceeded uses matching caps.
    {
        RenderCounts counts;
        expect(!BudgetsExceeded(counts), "(c) zero counts within budget");
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
