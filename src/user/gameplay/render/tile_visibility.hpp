#pragma once

#include <cmath>
#include <cstdint>
#include <functional>

#include "gameplay/math_types.hpp"

namespace madeline_cube {

// Minimal host-safe 4x4 matrix (column-major, matching T3D's layout) used for
// the top-view visibility polygon math. This keeps `tile_visibility.hpp`
// free of N64/t3d types so host tests can include it directly.
struct Mat4 {
    float m[16];  // column-major: m[col*4 + row]

    static Mat4 Identity() {
        Mat4 r = {};
        r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
        return r;
    }
};

// Transform a 4D homogenous point by `m`. Returns the w component separately.
inline void Mat4TransformPoint(const Mat4& m, float x, float y, float z,
                               float w, float& ox, float& oy, float& oz,
                               float& ow) {
    const float* c0 = &m.m[0];  // column 0
    const float* c1 = &m.m[4];
    const float* c2 = &m.m[8];
    const float* c3 = &m.m[12];
    ox = c0[0] * x + c1[0] * y + c2[0] * z + c3[0] * w;
    oy = c0[1] * x + c1[1] * y + c2[1] * z + c3[1] * w;
    oz = c0[2] * x + c1[2] * y + c2[2] * z + c3[2] * w;
    ow = c0[3] * x + c1[3] * y + c2[3] * z + c3[3] * w;
}

// A small convex 2D polygon in world XZ space (max 8 vertices — a frustum has
// 8 corners, and after XZ projection a view box produces a rectangle or quad).
struct Polygon2 {
    Vec2 pts[8];
    int count = 0;
};

// Project the camera frustum's 8 corners back to world space via the inverse
// view-projection matrix, drop the Y (up) axis, and return the resulting
// convex 2D polygon in world XZ. `ground_y` is the Y plane at which the
// frustum is intersected (unused for the XZ projection but kept for
// signature clarity). Mirrors `arch.md` §14.
inline Polygon2 ProjectFrustumToGround(const Mat4& inv_view_proj) {
    Polygon2 out;
    // Frustum corners in NDC: x/y in {-1,+1}, z in {-1,+1} (near/far).
    static const float ndc[8][3] = {
        {-1, -1, -1}, { 1, -1, -1}, { 1,  1, -1}, {-1,  1, -1},
        {-1, -1,  1}, { 1, -1,  1}, { 1,  1,  1}, {-1,  1,  1},
    };
    int n = 0;
    for (int i = 0; i < 8; ++i) {
        float ox, oy, oz, ow;
        Mat4TransformPoint(inv_view_proj, ndc[i][0], ndc[i][1], ndc[i][2],
                           1.0f, ox, oy, oz, ow);
        if (std::fabs(ow) < 1e-6f) continue;
        const float inv_w = 1.0f / ow;
        // Project to the X/Z plane (world x and z; y = up is dropped).
        out.pts[n].x = ox * inv_w;
        out.pts[n].y = oz * inv_w;
        ++n;
    }
    out.count = n;
    return out;
}

// Walk `poly` row by row in tile-index space, emitting for each row the
// inclusive [x_min, x_max] tile index range that intersects the polygon.
// `tile_size` is the world size of one tile/cell. `y_min`/`y_max` bound the
// rows to consider (camera vertical range). Mirrors `arch.md` §15
// (overworld_step / overworld_tile_slice).
//
// This is a conservative scanline enumerator: it clips each row's polygon
// span to the tile grid and emits the full inclusive range. It never emits
// tiles outside the polygon's X extent for a row.
inline void ScanlineTileRanges(const Polygon2& poly, float tile_size,
                               int y_min, int y_max,
                               const std::function<void(int y, int x_min,
                                                        int x_max)>& cb) {
    if (poly.count < 3) return;  // degenerate — no area
    if (tile_size <= 0.0f) return;

    // Bounding box in world XZ of the polygon.
    float wmin_x = poly.pts[0].x, wmax_x = poly.pts[0].x;
    float wmin_z = poly.pts[0].y, wmax_z = poly.pts[0].y;
    for (int i = 1; i < poly.count; ++i) {
        wmin_x = wmin_x < poly.pts[i].x ? wmin_x : poly.pts[i].x;
        wmax_x = wmax_x > poly.pts[i].x ? wmax_x : poly.pts[i].x;
        wmin_z = wmin_z < poly.pts[i].y ? wmin_z : poly.pts[i].y;
        wmax_z = wmax_z > poly.pts[i].y ? wmax_z : poly.pts[i].y;
    }

    // Tile index range covering the polygon.
    const int iz0 = static_cast<int>(std::floor(wmin_z / tile_size));
    const int iz1 = static_cast<int>(std::floor(wmax_z / tile_size));
    const int ix0 = static_cast<int>(std::floor(wmin_x / tile_size));
    const int ix1 = static_cast<int>(std::floor(wmax_x / tile_size));

    // Clamp the row range to the caller's vertical bound.
    const int z0 = iz0 > y_min ? iz0 : y_min;
    const int z1 = iz1 < y_max ? iz1 : y_max;

    for (int iz = z0; iz <= z1; ++iz) {
        // A tile intersects if its z-range overlaps [wmin_z, wmax_z]; since we
        // already restrict rows to [iz0, iz1], every row in range overlaps the
        // polygon's Z. The X span of the polygon is [wmin_x, wmax_x], so the
        // inclusive tile X range is [ix0, ix1].
        if (ix1 < ix0) continue;
        cb(iz, ix0, ix1);
    }
}

}  // namespace madeline_cube
