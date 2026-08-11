// Host test for the top-view visibility math (Pattern A: header-only, no N64
// deps). Asserts:
//   - `ProjectFrustumToGround` projects a synthetic frustum's corners back to
//     a known world XZ polygon.
//   - `ScanlineTileRanges` enumerates the expected tile footprint for simple
//     axis-aligned view boxes at a known tile size.
//
// Mirrors `arch.md` §14-15. The frustum polygon math and the scanline
// enumerator are validated independently of the N64 renderer.
//
// Build:
//   g++ -std=c++17 -Isrc/user tests/tile_visibility_contract.cpp
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <vector>

#include "gameplay/render/tile_visibility.hpp"

using namespace madeline_cube;

static int failures = 0;

static void expect(bool cond, const char* what) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

static bool near_eq(float a, float b, float eps = 1e-3f) {
    return std::fabs(a - b) < eps;
}

// A helper that builds an inverse view-projection matrix which maps NDC
// corners to a world-space box. We construct a simple orthographic-style
// inverse matrix by hand: it maps NDC {x,y,z} to world {x,y,z} via a fixed
// scale + translation. This exercises the corner-projection path without
// needing a full perspective camera.
static Mat4 MakeBoxInvViewProj(float world_half_extent_x,
                               float world_half_extent_z,
                               float world_center_x,
                               float world_center_z,
                               float world_y) {
    Mat4 m = Mat4::Identity();
    // NDC x in [-1,1] -> world x in [cx - hx, cx + hx]
    m.m[0] = world_half_extent_x;               // col0 row0: scale x
    m.m[12] = world_center_x;                   // col3 row0: translate x
    // NDC y in [-1,1] -> world y constant (drop at ground plane)
    m.m[5] = 0.0f;                              // col1 row1: no y scaling
    m.m[13] = world_y;                          // col3 row1: constant y
    // NDC z in [-1,1] -> world z in [cz - hz, cz + hz]
    m.m[10] = world_half_extent_z;              // col2 row2: scale z
    m.m[14] = world_center_z;                   // col3 row2: translate z
    return m;
}

int main() {
    const float kTileSize = 240.0f;

    // --- ProjectFrustumToGround ---
    // A symmetric box around the origin, half-extent 240 (one tile) in X and
    // Z. The 8 corners project to the rectangle x,z in [-240, 240], i.e. the
    // polygon should have 4 distinct 2D points (x and z drop Y).
    {
        const Mat4 inv = MakeBoxInvViewProj(240.0f, 240.0f, 0.0f, 0.0f, 0.0f);
        const Polygon2 poly = ProjectFrustumToGround(inv);
        expect(poly.count >= 4, "box frustum projects to >=4 2D corners");
        float min_x = poly.pts[0].x, max_x = poly.pts[0].x;
        float min_z = poly.pts[0].y, max_z = poly.pts[0].y;
        for (int i = 0; i < poly.count; ++i) {
            min_x = min_x < poly.pts[i].x ? min_x : poly.pts[i].x;
            max_x = max_x > poly.pts[i].x ? max_x : poly.pts[i].x;
            min_z = min_z < poly.pts[i].y ? min_z : poly.pts[i].y;
            max_z = max_z > poly.pts[i].y ? max_z : poly.pts[i].y;
        }
        expect(near_eq(min_x, -240.0f, 1e-2f) && near_eq(max_x, 240.0f, 1e-2f),
               "box frustum world X extent is [-240, 240]");
        expect(near_eq(min_z, -240.0f, 1e-2f) && near_eq(max_z, 240.0f, 1e-2f),
               "box frustum world Z extent is [-240, 240]");
    }

    // --- ScanlineTileRanges ---
    // A box cleanly inside one tile: world x,z in [10, 230] (within tile 0).
    // Spans exactly 1 row, x tile range [0,0].
    {
        const Mat4 inv = MakeBoxInvViewProj(110.0f, 110.0f, 120.0f, 120.0f, 0.0f);
        const Polygon2 poly = ProjectFrustumToGround(inv);
        struct Range { int y, x_min, x_max; };
        std::vector<Range> rows;
        ScanlineTileRanges(poly, kTileSize, -8, 8,
                           [&](int y, int x_min, int x_max) {
                               rows.push_back({y, x_min, x_max});
                           });
        expect(static_cast<int>(rows.size()) == 1, "single-tile box spans 1 row");
        if (rows.size() == 1) {
            expect(rows[0].x_min == 0 && rows[0].x_max == 0,
                   "single-tile box row spans x [0,0]");
        }
    }

    // A box cleanly inside tile -1 on Z and tile -1 on X: world x,z in
    // [-230, -10]. Spans 1 row at y=-1, x range [-1,-1].
    {
        const Mat4 inv = MakeBoxInvViewProj(110.0f, 110.0f, -120.0f, -120.0f, 0.0f);
        const Polygon2 poly = ProjectFrustumToGround(inv);
        struct Range { int y, x_min, x_max; };
        std::vector<Range> rows;
        ScanlineTileRanges(poly, kTileSize, -8, 8,
                           [&](int y, int x_min, int x_max) {
                               rows.push_back({y, x_min, x_max});
                           });
        expect(static_cast<int>(rows.size()) == 1, "negative single-tile box spans 1 row");
        if (rows.size() == 1) {
            expect(rows[0].y == -1, "negative single-tile box row is y=-1");
            expect(rows[0].x_min == -1 && rows[0].x_max == -1,
                   "negative single-tile box row spans x [-1,-1]");
        }
    }

    // A box spanning two tile rows: world x,z in [-230, 230] (cleanly inside
    // tiles -1 and 0 on both axes). Spans 2 rows (y=-1,0), each x range
    // [-1,0].
    {
        const Mat4 inv = MakeBoxInvViewProj(230.0f, 230.0f, 0.0f, 0.0f, 0.0f);
        const Polygon2 poly = ProjectFrustumToGround(inv);
        struct Range { int y, x_min, x_max; };
        std::vector<Range> rows;
        ScanlineTileRanges(poly, kTileSize, -8, 8,
                           [&](int y, int x_min, int x_max) {
                               rows.push_back({y, x_min, x_max});
                           });
        expect(static_cast<int>(rows.size()) == 2, "two-tile box spans exactly 2 rows");
        for (const Range& r : rows) {
            expect(r.x_min == -1 && r.x_max == 0,
                   "each two-tile box row spans x [-1, 0]");
        }
    }

    // Conservative boundary behavior: a box whose max exactly touches a tile
    // boundary (world x,z == 240) over-includes the boundary tile (index 1).
    // This is intentional — for visibility, over-inclusion is safe.
    {
        const Mat4 inv = MakeBoxInvViewProj(240.0f, 240.0f, 0.0f, 0.0f, 0.0f);
        const Polygon2 poly = ProjectFrustumToGround(inv);
        struct Range { int y, x_min, x_max; };
        std::vector<Range> rows;
        ScanlineTileRanges(poly, kTileSize, -8, 8,
                           [&](int y, int x_min, int x_max) {
                               rows.push_back({y, x_min, x_max});
                           });
        // floor(240/240)=1, floor(-240/240)=-1 -> rows -1,0,1 and x [-1,1].
        expect(static_cast<int>(rows.size()) == 3,
               "boundary-touching box over-includes row index 1 (conservative)");
        if (!rows.empty()) {
            expect(rows[0].x_max == 1,
                   "boundary-touching box over-includes x index 1 (conservative)");
        }
    }

    // Degenerate: an empty polygon (count < 3) produces no rows.
    {
        Polygon2 empty;
        empty.count = 2;
        int calls = 0;
        ScanlineTileRanges(empty, kTileSize, -8, 8,
                           [&](int, int, int) { ++calls; });
        expect(calls == 0, "degenerate polygon emits no rows");
    }

    // Degenerate: non-positive tile size emits no rows.
    {
        const Mat4 inv = MakeBoxInvViewProj(230.0f, 230.0f, 0.0f, 0.0f, 0.0f);
        const Polygon2 poly = ProjectFrustumToGround(inv);
        int calls = 0;
        ScanlineTileRanges(poly, 0.0f, -8, 8, [&](int, int, int) { ++calls; });
        expect(calls == 0, "non-positive tile size emits no rows");
    }

    if (failures == 0) {
        std::printf("tile_visibility_contract: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "tile_visibility_contract: %d failures\n", failures);
    return 1;
}
