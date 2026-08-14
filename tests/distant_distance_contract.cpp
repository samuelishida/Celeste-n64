// Host test for the distant distance² falloff (Pattern A: header-only, no N64
// deps). Asserts (Inc 2 / D1 + review MUST-FIX):
//   (a) cells within `max_dist2` survive, beyond are skipped;
//   (b) `max_dist2=0` → all pass (no-limit default, preserves the existing
//       `distant_cull_contract` behavior);
//   (c) monotonic — increasing threshold admits more cells;
//   (d) `CellWithinDistance` handles the `max_dist2 <= 0` no-limit rule.
//
// Build:
//   g++ -std=c++17 -Isrc/user tests/distant_distance_contract.cpp
#include <cstdio>

#include "gameplay/render/distant_world_renderer.hpp"
#include "gameplay/render/lod_math.hpp"

using namespace madeline_cube;

static int failures = 0;

static void expect(bool cond, const char* what) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

int main() {
    // (d) CellWithinDistance no-limit rule.
    {
        const Vec3 cam = {0.0f, 0.0f, 0.0f};
        const Vec3 far = {1000.0f, 0.0f, 1000.0f};
        expect(CellWithinDistance(cam, far, 0.0f),
               "(d) max_dist2=0 means no limit (far cell passes)");
        expect(CellWithinDistance(cam, far, -1.0f),
               "(d) negative max_dist2 means no limit");
        expect(!CellWithinDistance(cam, far, 100.0f),
               "(d) far cell beyond threshold is skipped");
        expect(CellWithinDistance(cam, {10.0f, 0.0f, 0.0f}, 100.0f),
               "(d) near cell within threshold survives");
    }

    // Build a small LOD table: 3 cells along +X at distances 100, 300, 600.
    DistantLodEntry entries[3] = {};
    entries[0].origin = {100.0f, 0.0f, 0.0f};
    entries[1].origin = {300.0f, 0.0f, 0.0f};
    entries[2].origin = {600.0f, 0.0f, 0.0f};

    const Vec3 cam = {0.0f, 0.0f, 0.0f};
    const Vec3 target_px = {1.0f, 0.0f, 0.0f};  // facing +X

    // (a) cells within max_dist2 survive, beyond are skipped.
    {
        DistantRenderItem out[3] = {};
        // max_dist2 = 200^2 = 40000 → only the 100 cell survives.
        const int n = BuildDistantRenderListCulled(
            cam, target_px, entries, 3, out, 3, 60.0f, 0.0f, 1000.0f,
            1.15f, 40000.0f);
        expect(n == 1, "(a) only the 100 cell within max_dist2=200 survives");
        if (n == 1) expect(out[0].cell_index == 0, "(a) survivor is cell 0");
    }

    // (b) max_dist2=0 → all pass (no-limit default).
    {
        DistantRenderItem out[3] = {};
        const int n = BuildDistantRenderListCulled(
            cam, target_px, entries, 3, out, 3, 60.0f, 0.0f, 1000.0f);
        expect(n == 3, "(b) max_dist2=0 (default) admits all in-frustum cells");
    }

    // (c) monotonic — increasing threshold admits more cells.
    {
        DistantRenderItem out[3] = {};
        const int n_small = BuildDistantRenderListCulled(
            cam, target_px, entries, 3, out, 3, 60.0f, 0.0f, 1000.0f,
            1.15f, 100.0f * 100.0f);  // only the 100 cell
        const int n_med = BuildDistantRenderListCulled(
            cam, target_px, entries, 3, out, 3, 60.0f, 0.0f, 1000.0f,
            1.15f, 300.0f * 300.0f);  // 100 + 300
        const int n_large = BuildDistantRenderListCulled(
            cam, target_px, entries, 3, out, 3, 60.0f, 0.0f, 1000.0f,
            1.15f, 600.0f * 600.0f);  // all three
        expect(n_small == 1 && n_med == 2 && n_large == 3,
               "(c) increasing max_dist2 admits more cells (monotonic)");
    }

    if (failures == 0) {
        std::printf("distant_distance_contract: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "distant_distance_contract: %d check(s) failed\n",
                 failures);
    return 1;
}
