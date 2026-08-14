// Host test for distant-pass frustum culling (Pattern A: header-only, no N64
// deps). Asserts:
//   - `CellInDistantFrustum` keeps cells inside the camera's horizontal view
//     cone + depth range and culls cells outside / behind / too close / beyond
//     far.
//   - `BuildDistantRenderListCulled` emits only in-frustum cells while
//     preserving the `BuildDistantRenderList` ordering contract (priority
//     increasing with distance) and capacity/null guards.
//
// Mirrors Inc 2 / D2 of the N64 perf fixup (45 distant cells → ~10-15 drawn).
//
// Build:
//   g++ -std=c++17 -Isrc/user tests/distant_cull_contract.cpp
#include <cmath>
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
    const Vec3 origin = {0.0f, 0.0f, 0.0f};
    const Vec3 target_px = {100.0f, 0.0f, 0.0f};   // facing +X
    const Vec3 target_nx = {-100.0f, 0.0f, 0.0f};  // facing -X

    // --- CellInDistantFrustum unit checks ---
    {
        // Cell straight ahead at 240 units: visible.
        expect(CellInDistantFrustum(origin, target_px, 60.0f, 50.0f, 1000.0f,
                                    {240.0f, 0.0f, 0.0f}),
               "straight-ahead cell is visible");
        // Cell 90° off the facing (+Z): culled at hfov 60 + margin 1.15.
        expect(!CellInDistantFrustum(origin, target_px, 60.0f, 50.0f, 1000.0f,
                                     {0.0f, 0.0f, 240.0f}),
               "90-degree-off cell is culled");
        // Cell behind the camera (-X): culled.
        expect(!CellInDistantFrustum(origin, target_px, 60.0f, 50.0f, 1000.0f,
                                     {-240.0f, 0.0f, 0.0f}),
               "behind-camera cell is culled");
        // Too close (dist < near): culled.
        expect(!CellInDistantFrustum(origin, target_px, 60.0f, 50.0f, 1000.0f,
                                     {10.0f, 0.0f, 0.0f}),
               "cell inside near plane is culled");
        // Beyond far: culled.
        expect(!CellInDistantFrustum(origin, target_px, 60.0f, 50.0f, 500.0f,
                                     {2400.0f, 0.0f, 0.0f}),
               "cell beyond far plane is culled");
        // Empty depth range (near >= far): never visible.
        expect(!CellInDistantFrustum(origin, target_px, 60.0f, 500.0f, 500.0f,
                                     {240.0f, 0.0f, 0.0f}),
               "near>=far frustum is empty");
        // Diagonal cell visible when the cone is wide enough (hfov 90 → half
        // 45° × 1.15 ≈ 51.75° > 45° diagonal).
        expect(CellInDistantFrustum(origin, target_px, 90.0f, 50.0f, 1000.0f,
                                    {240.0f, 0.0f, 240.0f}),
               "diagonal cell visible in wide cone");
        // Same diagonal culled in the narrower cone.
        expect(!CellInDistantFrustum(origin, target_px, 60.0f, 50.0f, 1000.0f,
                                     {240.0f, 0.0f, 240.0f}),
               "diagonal cell culled in narrow cone");
    }

    // --- BuildDistantRenderListCulled ---
    // 5 cells around the camera (cell size ~240).
    DistantLodEntry entries[5] = {};
    entries[0].origin = {240.0f, 0.0f, 0.0f};    // +X, dist 240
    entries[1].origin = {480.0f, 0.0f, 0.0f};    // +X, dist 480
    entries[2].origin = {0.0f, 0.0f, 240.0f};    // +Z, dist 240
    entries[3].origin = {-240.0f, 0.0f, 0.0f};   // -X, dist 240
    entries[4].origin = {0.0f, 0.0f, 0.0f};      // center, dist 0 (< near)
    for (int i = 0; i < 5; ++i) {
        entries[i].cell_ix = i;
        entries[i].cell_iz = 0;
    }

    {
        // Facing +X: only the +X cells survive (dist 240, 480 both in the
        // cone and within [50, 1000]); center is inside near, ±Z/-X are off
        // cone.
        DistantRenderItem out[5] = {};
        const int n = BuildDistantRenderListCulled(
            origin, target_px, entries, 5, out, 5, 60.0f, 50.0f, 1000.0f);
        expect(n == 2, "facing +X keeps only the +X cells");
        if (n == 2) {
            const bool has0 = (out[0].cell_index == 0 || out[1].cell_index == 0);
            const bool has1 = (out[0].cell_index == 1 || out[1].cell_index == 1);
            expect(has0 && has1, "the two +X cells are present");
        }
        // Ordering contract preserved: priority strictly increases with
        // distance along the emitted list (480 before 240 is a lower priority,
        // so priority[0] < priority[1]).
        if (n >= 2) {
            expect(out[0].priority < out[1].priority,
                   "culled list keeps distance-ordered priority");
        }
    }

    {
        // Facing -X: the +X cells are culled, the -X cell survives.
        DistantRenderItem out[5] = {};
        const int n = BuildDistantRenderListCulled(
            origin, target_nx, entries, 5, out, 5, 60.0f, 50.0f, 1000.0f);
        expect(n == 1, "facing -X keeps only the -X cell");
        if (n == 1) expect(out[0].cell_index == 3, "survivor is the -X cell");
    }

    {
        // Full reverse: only +X cells present, camera facing -X → 0 cells.
        DistantRenderItem out[2] = {};
        const int n = BuildDistantRenderListCulled(
            origin, target_nx, entries, 2, out, 2, 60.0f, 50.0f, 1000.0f);
        expect(n == 0, "camera pointed away from all cells draws nothing");
    }

    {
        // Depth clamps: near=300 culls the 240 cell (inside near) and keeps
        // the 480 cell; far=400 culls the 480 cell and keeps the 240 cell.
        DistantRenderItem out[5] = {};
        const int n_near = BuildDistantRenderListCulled(
            origin, target_px, entries, 2, out, 5, 60.0f, 300.0f, 1000.0f);
        expect(n_near == 1 && out[0].cell_index == 1,
               "near clamp keeps only the 480 cell");
        const int n_far = BuildDistantRenderListCulled(
            origin, target_px, entries, 2, out, 5, 60.0f, 0.0f, 400.0f);
        expect(n_far == 1 && out[0].cell_index == 0,
               "far clamp keeps only the 240 cell");
    }

    // Capacity + null guards.
    {
        DistantRenderItem out[1] = {};
        const int n = BuildDistantRenderListCulled(
            origin, target_px, entries, 5, out, 1, 60.0f, 50.0f, 1000.0f);
        expect(n == 1, "culled list bounded by capacity");
        expect(BuildDistantRenderListCulled(origin, target_px, entries, 5,
                                            nullptr, 5, 60.0f, 50.0f, 1000.0f) == 0,
               "null out returns 0");
        expect(BuildDistantRenderListCulled(origin, target_px, entries, 5,
                                            out, 0, 60.0f, 50.0f, 1000.0f) == 0,
               "zero capacity returns 0");
        expect(BuildDistantRenderListCulled(origin, target_px, nullptr, 0,
                                            out, 5, 60.0f, 50.0f, 1000.0f) == 0,
               "null entries returns 0");
    }

    // World-space convention (Inc 1): near = ring edge (1.5 × tile_size),
    // far = full map diagonal. A cell inside the ring diagonal is culled; a
    // cell just past the ring edge is kept; a far cell near the map diagonal
    // is kept; a cell beyond far is culled.
    {
        const float tile_size = 240.0f;
        const float near_d = tile_size * 1.5f;  // 360 (ring edge)
        const AABB world_bounds = {{-840.0f, 0.0f, -840.0f}, {840.0f, 0.0f, 840.0f}};
        const float far_d = MapFarClipDistance(&world_bounds, 1.15f);

        // Cell inside the ring diagonal (dist 240 < near 360): culled.
        expect(!CellInDistantFrustum(origin, target_px, 60.0f, near_d, far_d,
                                     {240.0f, 0.0f, 0.0f}),
               "cell inside ring diagonal is culled (dist < near)");
        // Cell just past the ring edge (dist 480 > near 360): kept.
        expect(CellInDistantFrustum(origin, target_px, 60.0f, near_d, far_d,
                                    {480.0f, 0.0f, 0.0f}),
               "cell just past ring edge is kept");
        // Far cell near the map diagonal (straight ahead, within cone): kept.
        expect(CellInDistantFrustum(origin, target_px, 60.0f, near_d, far_d,
                                    {far_d * 0.8f, 0.0f, 0.0f}),
               "far cell near map diagonal is kept");
        // Cell beyond far: culled.
        expect(!CellInDistantFrustum(origin, target_px, 60.0f, near_d, far_d,
                                     {far_d + 100.0f, 0.0f, 0.0f}),
               "cell beyond far is culled");
    }

    // --- CellAabbInDistantFrustum (Inc 4 / D3) ---
    {
        // A 240u cell centered at (240, 0, 0), facing +X.
        const AABB cell = {{120.0f, 0.0f, -120.0f}, {360.0f, 0.0f, 120.0f}};
        // Straight ahead: kept.
        expect(CellAabbInDistantFrustum(origin, target_px, 60.0f, 50.0f, 1000.0f,
                                        cell),
               "AABB straight ahead is kept");
        // Cell straddling the cone edge: center outside the cone but the AABB
        // reaches into it → kept (the screen-edge pop-in fix).
        {
            // Cell centered at (240, 0, 240): center is 45° off +X (outside
            // the 34.5° margined half-cone), but its near corner (360, 0, 120)
            // is at atan(120/360) ≈ 18.4° (inside) → the AABB reaches into the
            // cone, so the cell must be kept.
            const AABB edge_cell = {{120.0f, 0.0f, 120.0f}, {360.0f, 0.0f, 360.0f}};
            expect(CellAabbInDistantFrustum(origin, target_px, 60.0f, 50.0f,
                                            1000.0f, edge_cell),
                   "cell straddling cone edge is kept (AABB reaches into cone)");
        }
        // All 4 corners outside but the AABB intersects the cone interior →
        // kept (the exact bug class being fixed).
        {
            // A large cell centered just off the cone edge; the cone passes
            // through its interior even though all 4 corners are outside.
            const AABB big = {{-200.0f, 0.0f, 200.0f}, {200.0f, 0.0f, 600.0f}};
            expect(CellAabbInDistantFrustum(origin, target_px, 60.0f, 50.0f,
                                            1000.0f, big),
                   "AABB intersecting cone interior is kept (all corners outside)");
        }
        // Cell fully behind a side plane (AABB clear of the cone): culled.
        {
            const AABB behind = {{-400.0f, 0.0f, -100.0f}, {-200.0f, 0.0f, 100.0f}};
            expect(!CellAabbInDistantFrustum(origin, target_px, 60.0f, 50.0f,
                                             1000.0f, behind),
                   "AABB fully behind a side plane is culled");
        }
        // Cell within near-slack: kept even though its center is inside near.
        {
            const AABB near_cell = {{0.0f, 0.0f, -100.0f}, {200.0f, 0.0f, 100.0f}};
            expect(CellAabbInDistantFrustum(origin, target_px, 60.0f, 50.0f,
                                            1000.0f, near_cell),
                   "AABB within near-slack is kept");
        }
        // Zero-extent AABB → treated as the cell center (today's behavior).
        {
            const AABB zero = {{240.0f, 0.0f, 0.0f}, {240.0f, 0.0f, 0.0f}};
            expect(CellAabbInDistantFrustum(origin, target_px, 60.0f, 50.0f,
                                            1000.0f, zero),
                   "zero-extent AABB behaves like the cell center");
        }
        // Depth-range edge: cell beyond far (with slack) is culled.
        {
            const AABB far_cell = {{2400.0f, 0.0f, -100.0f}, {2600.0f, 0.0f, 100.0f}};
            expect(!CellAabbInDistantFrustum(origin, target_px, 60.0f, 50.0f,
                                             1000.0f, far_cell),
                   "AABB beyond far is culled");
        }
    }

    if (failures == 0) {
        std::printf("distant_cull_contract: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "distant_cull_contract: %d failures\n", failures);
    return 1;
}
