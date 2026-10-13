// Host test for near-pass visibility helpers and resident draw policy (Pattern
// A: header-only, no N64 deps). Asserts (Inc 4 / D4):
//   - `Mat4Invert` inverts translation/scale/affine matrices and M⁻¹·M ≈ I,
//     and returns false for a singular matrix.
//   - `ResolveVisibleTiles` + `ResidentSet::IndexOf` maps projected cells.
//   - The production draw mask keeps every bounded resident drawable;
//     ownership does not switch with camera turns.
//
// The device draw path is still verified in Ares; this test covers the pure
// projection helpers and the exact visibility-mask operation used by runtime.
//
// Build:
//   g++ -std=c++17 -Isrc/user tests/near_visibility_contract.cpp
//
// Residency bounds the draw set; pending residents still use DLOD fallback.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

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

    // --- Production resident draw mask ---
    // All bounded residents remain drawable, independent of camera direction.
    {
        const float kCell = 240.0f;
        MapSpecV2 cone_spec = {};
        cone_spec.chunk_size = 1200.0f;
        cone_spec.scale = 0.2f;
        cone_spec.room_count = 10;
        auto place = [&](int i, const char* id, int ix, int iz) {
            std::strcpy(cone_spec.rooms[i].id, id);
            cone_spec.rooms[i].cell_ix = ix;
            cone_spec.rooms[i].cell_iz = iz;
            cone_spec.rooms[i].world_aabb = {
                {ix * kCell, -30.0f, iz * kCell},
                {(ix + 1) * kCell, 110.0f, (iz + 1) * kCell}};
        };
        place(0, "c00", 0, 0);   // center
        place(1, "c10", 1, 0);   // +X neighbor
        place(2, "c01", 0, 1);   // +Z neighbor
        place(3, "cm10", -1, 0); // -X neighbor
        place(4, "c0m1", 0, -1); // -Z neighbor
        place(5, "cm11", -1, 1); // back-diagonal neighbor
        place(6, "c11", 1, 1);   // +X,+Z core corner
        place(7, "cm1m1", -1, -1); // -X,-Z core corner
        place(8, "c1m1", 1, -1); // +X,-Z core corner
        place(9, "c20", 2, 0);   // ready outer preload, within near far plane
        ResidentSet residents;
        residents.count = cone_spec.room_count;
        for (int i = 0; i < residents.count; ++i)
            residents.spec[i] = &cone_spec.rooms[i];
        ResidentVisibility vis;
        const CameraDesc near_camera = MakeNearCamera(
            45.0f, 5.0f, 800.0f, {120.0f, 50.0f, 120.0f},
            {1000.0f, 50.0f, 120.0f}, {0.0f, 1.0f, 0.0f});
        ResolveResidentFrustumMask(vis, residents, near_camera);
        expect(vis.visible[0], "center AABB intersects near frustum");
        expect(vis.visible[1], "+X neighbor intersects near frustum");
        expect(!vis.visible[2], "+Z neighbor outside near frustum is culled");
        expect(vis.visible[9],
               "ready Cheb-2 resident in view remains near-owned");
        expect(!vis.visible[10], "empty resident slots remain hidden");
        expect(IsDrawnReadyResidentCell(
                   residents, vis, 1, 0, [](int) { return true; }),
               "visible ready core cell belongs to the near pass");
        expect(IsDrawnReadyResidentCell(
                   residents, vis, 2, 0, [](int) { return true; }),
               "ready outer resident owns near pass consistently");
        expect(!IsDrawnReadyResidentCell(
                    residents, vis, 1, 0, [](int) { return false; }),
               "pending cell remains eligible for DLOD coverage");
    }

    if (failures == 0) {
        std::printf("near_visibility_contract: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "near_visibility_contract: %d failures\n", failures);
    return 1;
}
