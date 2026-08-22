// Host test for near-pass visibility culling (Pattern A: header-only, no N64
// deps). Asserts (Inc 4 / D4):
//   - `Mat4Invert` inverts translation/scale/affine matrices and M⁻¹·M ≈ I,
//     and returns false for a singular matrix.
//   - `ResolveVisibleTiles` + `ResidentSet::IndexOf` mapping yields the
//     center-always-visible mask: a frustum covering only the center cell
//     marks only the center; a frustum covering a neighbor marks it; an empty
//     visible set falls back to the center only.
//
// The real `TileStreamer::UpdateCamera` wiring is device-only and covered by
// the device visual walk; this test exercises the pure projection + IndexOf
// mapping (the same logic UpdateCamera runs).
//
// Build:
//   g++ -std=c++17 -Isrc/user tests/near_visibility_contract.cpp
//
// n64-optimization Inc 3 extends this test (no duplicate file) with the
// `CellAabbInNearCone` predicate that `TileStreamer::UpdateCamera` runs when
// `kEnableNearCulling` is ON: for a given camera, a resident cell outside the
// (margined) cone is NOT drawn while the center cell always is (explicit
// exemption), and a degenerate facing (pos == target) draws safe-true.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "gameplay/render/lod_math.hpp"
#include "gameplay/render/tile_streamer.hpp"
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

// Column-major 4x4 multiply: C = A*B, C[col][row] = Σ_k B[col][k]*A[k][row].
static Mat4 Mat4Mul(const Mat4& a, const Mat4& b) {
    Mat4 c = {};
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            float s = 0.0f;
            for (int k = 0; k < 4; ++k) s += b.m[col * 4 + k] * a.m[k * 4 + row];
            c.m[col * 4 + row] = s;
        }
    }
    return c;
}

// A synthetic inverse view-projection that maps NDC {x,y,z} to world via a
// fixed scale + translation (same pattern as tile_visibility_contract.cpp).
static Mat4 MakeBoxInvViewProj(float world_half_extent_x,
                               float world_half_extent_z,
                               float world_center_x,
                               float world_center_z,
                               float world_y) {
    Mat4 m = Mat4::Identity();
    m.m[0] = world_half_extent_x;     // NDC x -> world x
    m.m[12] = world_center_x;
    m.m[5] = 0.0f;                    // no y scaling (drop at ground plane)
    m.m[13] = world_y;
    m.m[10] = world_half_extent_z;    // NDC z -> world z
    m.m[14] = world_center_z;
    return m;
}

int main() {
    // --- Mat4Invert ---
    {
        // Translation inverse: T(10,20,30)⁻¹ maps (0,0,0) -> (-10,-20,-30).
        Mat4 t = Mat4::Identity();
        t.m[12] = 10.0f; t.m[13] = 20.0f; t.m[14] = 30.0f;
        expect(Mat4Invert(t), "translation invert succeeds");
        float ox, oy, oz, ow;
        Mat4TransformPoint(t, 0.0f, 0.0f, 0.0f, 1.0f, ox, oy, oz, ow);
        expect(near_eq(ox, -10.0f) && near_eq(oy, -20.0f) && near_eq(oz, -30.0f),
               "translation inverse maps origin to -translate");

        // Scale inverse: diag(2,3,4)⁻¹ maps (2,3,4) -> (1,1,1).
        Mat4 s = Mat4::Identity();
        s.m[0] = 2.0f; s.m[5] = 3.0f; s.m[10] = 4.0f;
        expect(Mat4Invert(s), "scale invert succeeds");
        Mat4TransformPoint(s, 2.0f, 3.0f, 4.0f, 1.0f, ox, oy, oz, ow);
        expect(near_eq(ox, 1.0f) && near_eq(oy, 1.0f) && near_eq(oz, 1.0f),
               "scale inverse maps (2,3,4) to (1,1,1)");

        // Affine M⁻¹·M ≈ I.
        Mat4 m = Mat4::Identity();
        m.m[0] = 2.0f; m.m[5] = 3.0f; m.m[10] = 4.0f;
        m.m[12] = 5.0f; m.m[13] = 6.0f; m.m[14] = 7.0f;
        Mat4 inv = m;
        expect(Mat4Invert(inv), "affine invert succeeds");
        const Mat4 prod = Mat4Mul(inv, m);
        bool ident = true;
        for (int c = 0; c < 4 && ident; ++c) {
            for (int r = 0; r < 4 && ident; ++r) {
                const float want = (c == r) ? 1.0f : 0.0f;
                if (!near_eq(prod.m[c * 4 + r], want)) ident = false;
            }
        }
        expect(ident, "M^-1 * M approximates identity");

        // Singular matrix → false.
        Mat4 singular = {};
        expect(!Mat4Invert(singular), "zero matrix is singular (returns false)");
    }

    // --- Visibility mapping ---
    const float kTileSize = 240.0f;  // chunk 1200 * scale 0.2
    MapSpecV2 spec = {};
    spec.chunk_size = 1200.0f;
    spec.scale = 0.2f;
    spec.room_count = 3;
    std::strcpy(spec.rooms[0].id, "cell_00_00");
    spec.rooms[0].cell_ix = 0; spec.rooms[0].cell_iz = 0;
    spec.rooms[0].render_origin = {0.0f, 0.0f, 0.0f};
    std::strcpy(spec.rooms[1].id, "cell_10_00");
    spec.rooms[1].cell_ix = 1; spec.rooms[1].cell_iz = 0;
    spec.rooms[1].render_origin = {240.0f, 0.0f, 0.0f};
    std::strcpy(spec.rooms[2].id, "cell_00_10");
    spec.rooms[2].cell_ix = 0; spec.rooms[2].cell_iz = 1;
    spec.rooms[2].render_origin = {0.0f, 0.0f, 240.0f};

    // A resident set mirroring SetCenter: index 0 = center (cell_00_00).
    ResidentSet set;
    set.spec[0] = &spec.rooms[0];
    set.spec[1] = &spec.rooms[1];
    set.spec[2] = &spec.rooms[2];
    set.count = 3;

    // Simulate TileStreamer::UpdateCamera's mask logic (center always visible).
    auto resolve_mask = [&](const Mat4& inv, bool vis[kMaxRing]) {
        for (int i = 0; i < kMaxRing; ++i) vis[i] = false;
        vis[0] = true;  // center always drawn
        const V2RoomSpec* out[kMaxRing] = {};
        const int n = ResolveVisibleTiles(spec, inv, 0.0f, kTileSize, out, kMaxRing);
        for (int v = 0; v < n; ++v) {
            const int idx = set.IndexOf(out[v]);
            if (idx >= 0 && idx < kMaxRing) vis[idx] = true;
        }
        return n;
    };

    // Frustum covering ONLY the center cell (world x,z in [-100,100], inside
    // tile (0,0)) → only the center is marked.
    {
        const Mat4 inv = MakeBoxInvViewProj(100.0f, 100.0f, 0.0f, 0.0f, 0.0f);
        bool vis[kMaxRing] = {};
        const int n = resolve_mask(inv, vis);
        expect(n == 1, "center-only frustum resolves 1 room");
        expect(vis[0] && !vis[1] && !vis[2],
               "center-only frustum marks center, culls neighbors");
    }

    // Frustum covering the +X neighbor (world x in [250,470] → tile x=1,
    // z in [-100,100] → tile z=0) → marks the neighbor (center still forced).
    {
        const Mat4 inv = MakeBoxInvViewProj(110.0f, 100.0f, 360.0f, 0.0f, 0.0f);
        bool vis[kMaxRing] = {};
        const int n = resolve_mask(inv, vis);
        expect(n >= 1, "neighbor frustum resolves >=1 room");
        expect(vis[0] && vis[1] && !vis[2],
               "neighbor frustum marks +X resident, culls +Z resident");
    }

    // Empty visible set (frustum covering far tiles with no rooms) → falls
    // back to center-only (no black frame).
    {
        const Mat4 inv = MakeBoxInvViewProj(100.0f, 100.0f, 1200.0f, 1200.0f, 0.0f);
        bool vis[kMaxRing] = {};
        const int n = resolve_mask(inv, vis);
        expect(n == 0, "empty frustum resolves 0 rooms");
        expect(vis[0] && !vis[1] && !vis[2],
               "empty frustum still marks the center (fallback)");
    }

    // --- n64-optimization Inc 3: CellAabbInNearCone (UpdateCamera wiring) ---
    // 3×3 grid of 240u cells (chunk 1200 * scale 0.2), minus two corners. The
    // camera sits at the center of cell (0,0) looking +X. The cone is the
    // vertical 45° FOV widened to horizontal (4:3) then by kCullMargin (1.15f),
    // plus a per-corner atan(half_diag / dist) slack. That per-corner slack is
    // generous: a 240u cell at 240u range widens the cone by ~35°, so the
    // effective half-angle is ~68° — off-axis ring cells whose NEAR corner
    // falls inside ~60° are drawn, and only cells behind the camera cull.
    // That is the intended draw-safe behavior (no screen-edge pop); the
    // back-diagonal cell below is the strong "resident outside the cone is
    // NOT drawn" assertion.
    {
        const float kCell = 240.0f;
        MapSpecV2 cone_spec = {};
        cone_spec.chunk_size = 1200.0f;
        cone_spec.scale = 0.2f;
        cone_spec.room_count = 6;
        auto place = [&](int i, const char* id, int ix, int iz) {
            std::strcpy(cone_spec.rooms[i].id, id);
            cone_spec.rooms[i].cell_ix = ix;
            cone_spec.rooms[i].cell_iz = iz;
            cone_spec.rooms[i].world_aabb = {
                {ix * kCell, 0.0f, iz * kCell},
                {(ix + 1) * kCell, 0.0f, (iz + 1) * kCell}};
        };
        place(0, "c00", 0, 0);   // center
        place(1, "c10", 1, 0);   // +X neighbor
        place(2, "c01", 0, 1);   // +Z neighbor
        place(3, "cm10", -1, 0); // -X neighbor
        place(4, "c0m1", 0, -1); // -Z neighbor
        place(5, "cm11", -1, 1); // back-diagonal neighbor

        const CameraDesc cam = MakeNearCamera(45.0f, 5.0f, 800.0f,
                                              Vec3{0.0f, 0.0f, 0.0f},
                                              Vec3{1000.0f, 0.0f, 0.0f},
                                              Vec3{0.0f, 1.0f, 0.0f});
        // Mirror UpdateCamera's mask logic: center exempt, cone test for the
        // rest.
        auto resolve_cone_mask = [&](const CameraDesc& c, bool vis[kMaxRing]) {
            for (int i = 0; i < kMaxRing; ++i) vis[i] = false;
            vis[0] = true;  // center always drawn (explicit exemption)
            for (int i = 1; i < cone_spec.room_count; ++i) {
                vis[i] = CellAabbInNearCone(c.pos, c.target, c.fov_deg, c.near,
                                            c.far, cone_spec.rooms[i].world_aabb);
            }
        };

        bool vis[kMaxRing] = {};
        resolve_cone_mask(cam, vis);
        expect(vis[0], "cone: center cell always drawn (exemption)");
        expect(vis[1], "cone: +X neighbor (in front) is drawn");
        expect(vis[2], "cone: +Z neighbor drawn (near corner inside widened cone)");
        expect(!vis[3], "cone: -X neighbor (behind) is culled");
        expect(vis[4], "cone: -Z neighbor drawn (corner on forward axis)");
        expect(!vis[5], "cone: back-diagonal neighbor is culled");

        // Degenerate facing (pos == target): the predicate returns
        // draw-safe-true for every cell — nothing is culled by a zero-length
        // facing.
        const CameraDesc deg = MakeNearCamera(45.0f, 5.0f, 800.0f,
                                              Vec3{0.0f, 0.0f, 0.0f},
                                              Vec3{0.0f, 0.0f, 0.0f},
                                              Vec3{0.0f, 1.0f, 0.0f});
        resolve_cone_mask(deg, vis);
        expect(vis[0] && vis[1] && vis[2] && vis[3] && vis[4] && vis[5],
               "cone: degenerate facing draws all residents (safe-true)");
    }

    if (failures == 0) {
        std::printf("near_visibility_contract: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "near_visibility_contract: %d failures\n", failures);
    return 1;
}
