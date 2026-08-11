// Host test for two-pass camera derivation (Pattern A: header-only, no N64
// deps). Asserts `BuildPassCameras` produces a distant camera matching
// `arch.md` §5 exactly:
//   distant.near == near.far * 0.25f * lod_scale
//   distant.far  == tile_size * 1.4f
// and that both passes share the same camera_pos/target/up orientation.
//
// Build:
//   g++ -std=c++17 -Isrc/user tests/pass_camera_math.cpp
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "gameplay/render/open_world_renderer.hpp"
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
    // 240-unit cells, near 20 / far 800, kLodScale = 0.25.
    const Vec3 cam_pos = {1000.0f, 50.0f, -300.0f};
    const Vec3 cam_tgt = {1024.0f, 50.0f, -300.0f};
    const float fov = 60.0f;
    const float near_p = 20.0f;
    const float far_p = 800.0f;
    const float tile_size = 240.0f;
    const float lod_scale = 0.25f;

    const PassCameras pc =
        BuildPassCameras(cam_pos, cam_tgt, fov, near_p, far_p, tile_size, lod_scale);

    // Near camera keeps the explicit clip planes and orientation.
    expect(near_eq(pc.near_cam.fov_deg, fov), "near fov preserved");
    expect(near_eq(pc.near_cam.near, near_p), "near near preserved");
    expect(near_eq(pc.near_cam.far, far_p), "near far preserved");
    expect(near_eq(pc.near_cam.pos.x, cam_pos.x) && near_eq(pc.near_cam.pos.z, cam_pos.z),
           "near pos preserved");

    // Distant camera per arch.md §5.
    const float exp_distant_near = far_p * 0.25f * lod_scale;  // 800*0.25*0.25 = 50
    const float exp_distant_far = tile_size * 1.4f;            // 240*1.4 = 336
    expect(near_eq(pc.distant_cam.near, exp_distant_near), "distant.near == near.far * 0.25 * lod_scale");
    expect(near_eq(pc.distant_cam.far, exp_distant_far), "distant.far == tile_size * 1.4");

    // Both passes share orientation (pos/target/up).
    expect(near_eq(pc.distant_cam.pos.x, cam_pos.x) && near_eq(pc.distant_cam.pos.z, cam_pos.z),
           "distant shares camera pos");
    expect(near_eq(pc.distant_cam.target.x, cam_tgt.x) && near_eq(pc.distant_cam.target.z, cam_tgt.z),
           "distant shares camera target");
    expect(near_eq(pc.distant_cam.up.y, 1.0f), "distant shares camera up");

    // The distant camera must have a valid, non-empty range for this config.
    expect(ValidateDistantCamera(pc.distant_cam), "distant camera valid (far > near > 0)");

    // lod_scale is a coordinate packing scale, NOT a clip-plane multiplier:
    // halving lod_scale must halve distant.near but leave distant.far fixed.
    const PassCameras pc2 =
        BuildPassCameras(cam_pos, cam_tgt, fov, near_p, far_p, tile_size, lod_scale * 0.5f);
    expect(near_eq(pc2.distant_cam.near, exp_distant_near * 0.5f),
           "halving lod_scale halves distant.near");
    expect(near_eq(pc2.distant_cam.far, exp_distant_far),
           "halving lod_scale leaves distant.far unchanged");

    // Edge case: a degenerate distant range must be rejected. Force
    // distant.near > distant.far by a huge lod_scale.
    const float huge_lod = 100.0f;  // distant.near = 800*0.25*100 = 20000 > 336
    const CameraDesc near_only = MakeNearCamera(fov, near_p, far_p, cam_pos, cam_tgt, {0, 1, 0});
    const CameraDesc bad_distant = MakeDistantCamera(near_only, tile_size, huge_lod);
    expect(!ValidateDistantCamera(bad_distant), "huge lod_scale -> invalid distant camera rejected");

    // Edge case: distant.near must be > 0. A zero lod_scale yields near = 0.
    const CameraDesc zero_distant = MakeDistantCamera(near_only, tile_size, 0.0f);
    expect(!ValidateDistantCamera(zero_distant), "zero lod_scale -> near=0 rejected");

    if (failures == 0) {
        std::printf("pass_camera_math: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "pass_camera_math: %d failures\n", failures);
    return 1;
}
