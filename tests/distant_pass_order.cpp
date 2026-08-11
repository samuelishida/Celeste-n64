// Host test for distant pass ordering (Pattern A: header-only, no N64 deps).
// Asserts `BuildDistantRenderList` assigns strictly increasing priority with
// distance (so far cells draw first in the back-to-front distant pass) and
// that the list is bounded by capacity. Mirrors arch.md §8.
//
// Build:
//   g++ -std=c++17 -Isrc/user tests/distant_pass_order.cpp
#include <cstdio>
#include <cstdlib>

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
    // Build a synthetic LOD table of 4 cells at increasing distance from the
    // camera.
    DistantLodEntry entries[4] = {};
    for (int i = 0; i < 4; ++i) {
        entries[i].origin = {float(i) * 500.0f, 0.0f, 0.0f};  // +X
        entries[i].cell_ix = i;
        entries[i].cell_iz = 0;
    }
    const Vec3 camera = {0.0f, 0.0f, 0.0f};

    DistantRenderItem out[4] = {};
    const int n = BuildDistantRenderList(camera, entries, 4, out, 4);
    expect(n == 4, "all 4 cells emitted");

    // Priority must be strictly increasing with distance (dist² monotonic).
    for (int i = 0; i < n; ++i) {
        expect(out[i].cell_index == i, "cell index maps back to table index");
        if (i > 0) {
            expect(out[i].priority > out[i - 1].priority,
                   "priority strictly increases with distance");
        }
    }

    // Capacity bound: asking for 2 slots returns at most 2.
    DistantRenderItem small[2] = {};
    const int m = BuildDistantRenderList(camera, entries, 4, small, 2);
    expect(m == 2, "output is bounded by capacity");

    // Null/zero guards.
    expect(BuildDistantRenderList(camera, entries, 4, nullptr, 4) == 0,
           "null out returns 0");
    expect(BuildDistantRenderList(camera, entries, 4, out, 0) == 0,
           "zero capacity returns 0");
    expect(BuildDistantRenderList(camera, nullptr, 0, out, 4) == 0,
           "null entries returns 0");

    if (failures == 0) {
        std::printf("distant_pass_order: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "distant_pass_order: %d failures\n", failures);
    return 1;
}
