// Host test for near/distant pass camera derivation (header-only, no N64 deps).
//
// Build:
//   g++ -std=c++17 -Isrc/user tests/pass_camera_math.cpp
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "gameplay/render/pass_camera_math.hpp"

using namespace madeline_cube;

static int failures = 0;

static void expect(bool cond, const char* what) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

static bool near_eq(float a, float b, float eps = 1e-4f) {
    return std::fabs(a - b) < eps;
}

int main() {
    // Representative world-space camera and near clip range.
    const Vec3 cam_pos = {1000.0f, 50.0f, -300.0f};
    const Vec3 cam_tgt = {1024.0f, 50.0f, -300.0f};
    const Vec3 up = {0.0f, 1.0f, 0.0f};
    const float fov = 45.0f;
    const float near_p = 5.0f;
    const float far_p = 800.0f;

    const CameraDesc c = MakeNearCamera(fov, near_p, far_p, cam_pos, cam_tgt, up);

    // Clip planes preserved exactly.
    expect(near_eq(c.fov_deg, fov), "fov preserved");
    expect(near_eq(c.near, near_p), "near preserved");
    expect(near_eq(c.far, far_p), "far preserved");

    // Orientation preserved (pos/target/up).
    expect(near_eq(c.pos.x, cam_pos.x) && near_eq(c.pos.y, cam_pos.y) && near_eq(c.pos.z, cam_pos.z),
           "pos preserved");
    expect(near_eq(c.target.x, cam_tgt.x) && near_eq(c.target.y, cam_tgt.y) && near_eq(c.target.z, cam_tgt.z),
           "target preserved");
    expect(near_eq(c.up.x, 0.0f) && near_eq(c.up.y, 1.0f) && near_eq(c.up.z, 0.0f),
           "up preserved");

    // A valid near camera always has far > near > 0.
    expect(c.far > c.near && c.near > 0.0f, "valid near range (far > near > 0)");

    // Defaults are sane for the single-pass renderer.
    const CameraDesc def;
    expect(near_eq(def.near, 5.0f), "default near == 5.0");
    expect(near_eq(def.far, 800.0f), "default near camera far == 800.0");

    // Distant projection shares the near plane so pending/nonresident cells
    // remain a fallback even when camera lag puts them inside the resident
    // ring's nominal radius. It still covers the full 3D map diagonal.
    const AABB bounds = {{0.0f, -50.0f, -2400.0f},
                         {1200.0f, 200.0f, 0.0f}};
    const CameraDesc distant = MakeDistantCamera(c, 240.0f, &bounds);
    const float expected_far = std::sqrt(1200.0f * 1200.0f +
                                         250.0f * 250.0f +
                                         2400.0f * 2400.0f) * 1.15f;
    expect(near_eq(distant.near, c.near),
           "distant near matches near pass to avoid clipped pending cells");
    expect(near_eq(distant.far, expected_far), "distant far covers 3D map diagonal");
    expect(ValidateDistantCamera(distant), "distant clip range is valid");
    expect(near_eq(distant.fov_deg, c.fov_deg) &&
               near_eq(distant.pos.x, c.pos.x) &&
               near_eq(distant.target.z, c.target.z),
           "distant camera preserves near camera orientation");

    const CameraDesc fallback = MakeDistantCamera(c, 240.0f, nullptr);
    expect(near_eq(fallback.near, c.near) && fallback.far > fallback.near,
           "missing bounds produce a safe non-empty distant range");

    // Camera can lag beyond the active-cell boundary during fast travel. A
    // nonresident cell two tiles ahead can then begin inside the old 1.5-tile
    // DLOD near plane and get sliced while its near mesh is still streaming.
    const CameraDesc edge_camera = MakeNearCamera(
        fov, near_p, far_p, {510.0f, 50.0f, 0.0f},
        {511.0f, 50.0f, 0.0f}, up);
    const AABB pending_cell = {{720.0f, 0.0f, -120.0f},
                               {960.0f, 100.0f, 120.0f}};
    const CameraDesc edge_distant = MakeDistantCamera(
        edge_camera, 240.0f, &bounds);
    expect(CellAabbInFrustum3D(edge_camera.pos, edge_camera.target,
                               edge_camera.up, edge_camera.fov_deg,
                               4.0f / 3.0f, edge_distant.near,
                               edge_distant.far, pending_cell),
           "pending off-ring cell remains DLOD-visible through the split");

    if (failures == 0) {
        std::printf("pass_camera_math: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "pass_camera_math: %d failures\n", failures);
    return 1;
}
