// Host test for single near-pass camera derivation (Pattern A: header-only,
// no N64 deps). After the z-split distant pass was removed, `MakeNearCamera`
// builds the one gameplay camera: explicit clip planes (near=5, far=800) and
// the caller's orientation. Asserts the clip planes and orientation are
// preserved exactly, and that defaults are sane.
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
    // Representative world-space camera + config matching the real map pack:
    // near 5 / far 800 (good-era values), camera-at-origin coupling.
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
    expect(near_eq(def.far, 800.0f), "default far == 800.0");

    if (failures == 0) {
        std::printf("pass_camera_math: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "pass_camera_math: %d failures\n", failures);
    return 1;
}
