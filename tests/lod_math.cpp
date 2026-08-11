// Host test for distant LOD selection + directional math (Pattern A:
// header-only, no N64 deps). Asserts `SelectLodLevel` and
// `DirectionalMeshIndex` return the expected indices for synthetic
// camera/tile configurations (distance thresholds, center-vs-periphery
// directional selection). Mirrors arch.md §10-12.
//
// Build:
//   g++ -std=c++17 -Isrc/user tests/lod_math.cpp
#include <cmath>
#include <cstdio>
#include <cstdlib>

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
    // --- DirectionalMeshIndex ---
    // Camera +X of the tile => EAST (index 2).
    {
        const Vec3 cam = {240.0f, 0.0f, 0.0f};
        const Vec3 origin = {0.0f, 0.0f, 0.0f};
        const Vec3 dir = {1.0f, 0.0f, 0.0f};
        expect(DirectionalMeshIndex(cam, origin, dir, 200.0f) == 2,
               "camera +X of tile -> EAST(2)");
    }
    // Camera -X of the tile => WEST (3).
    {
        const Vec3 cam = {-240.0f, 0.0f, 0.0f};
        const Vec3 origin = {0.0f, 0.0f, 0.0f};
        expect(DirectionalMeshIndex(cam, origin, {1, 0, 0}, 200.0f) == 3,
               "camera -X of tile -> WEST(3)");
    }
    // Camera +Z (depth) of the tile => SOUTH (0).
    {
        const Vec3 cam = {0.0f, 0.0f, 240.0f};
        const Vec3 origin = {0.0f, 0.0f, 0.0f};
        expect(DirectionalMeshIndex(cam, origin, {1, 0, 0}, 200.0f) == 0,
               "camera +Z of tile -> SOUTH(0)");
    }
    // Camera -Z (depth) of the tile => NORTH (1).
    {
        const Vec3 cam = {0.0f, 0.0f, -240.0f};
        const Vec3 origin = {0.0f, 0.0f, 0.0f};
        expect(DirectionalMeshIndex(cam, origin, {1, 0, 0}, 200.0f) == 1,
               "camera -Z of tile -> NORTH(1)");
    }
    // Camera very close to the tile center: uses the CAMERA direction, not
    // the tile->camera delta (arch.md §12). With the camera facing -X, the
    // close camera should select WEST (from camera_dir) rather than the
    // (ambiguous, near-zero) delta.
    {
        const Vec3 cam = {1.0f, 0.0f, 1.0f};  // within close_threshold of center
        const Vec3 origin = {0.0f, 0.0f, 0.0f};
        const Vec3 cam_dir = {-1.0f, 0.0f, 0.0f};  // facing -X
        expect(DirectionalMeshIndex(cam, origin, cam_dir, 200.0f) == 3,
               "close camera uses camera_dir -> WEST(3)");
    }
    // Diagonal dominance: |delta.x| >= |delta.z| picks the X axis.
    {
        const Vec3 cam = {300.0f, 0.0f, 100.0f};
        const Vec3 origin = {0.0f, 0.0f, 0.0f};
        expect(DirectionalMeshIndex(cam, origin, {0, 0, 0}, 200.0f) == 2,
               "diagonal with |dx|>|dz| -> EAST(2)");
    }

    // --- SelectLodLevel ---
    // Very close camera: distance² < level-0 threshold => level 0 (highest).
    {
        const Vec3 cam = {10.0f, 0.0f, 0.0f};
        const Vec3 origin = {0.0f, 0.0f, 0.0f};
        const int lvl = SelectLodLevel(cam, origin, 0.25f, 2);
        expect(lvl == 0, "very close -> LOD 0");
    }
    // Farther camera (beyond level-0 threshold but inside level-1) => level 1.
    // base = 500² * 0.25² = 15625; level-1 threshold = 2*15625 = 31250
    // (dist ~177). At dist 200 (dist²=40000) it is beyond level-1, so returns
    // the level where dist² < (1+level)*base stops. dist²=40000 < 3*15625?
    // 40000 < 46875 => level 2 (coarsest). At dist 150 (22500) it is
    // < 2*15625 => level 1. Test with dist 150 for a definite level-1.
    {
        const Vec3 cam = {150.0f, 0.0f, 0.0f};
        const Vec3 origin = {0.0f, 0.0f, 0.0f};
        const int lvl = SelectLodLevel(cam, origin, 0.25f, 2);
        expect(lvl == 1, "mid camera -> LOD 1");
    }
    // Very far camera: beyond all thresholds => coarsest (max_level).
    {
        const Vec3 cam = {5000.0f, 0.0f, 0.0f};
        const Vec3 origin = {0.0f, 0.0f, 0.0f};
        const int lvl = SelectLodLevel(cam, origin, 0.25f, 2);
        expect(lvl == 2, "very far -> LOD 2 (coarsest)");
    }
    // A smaller lod_scale shifts the thresholds: closer distances switch LOD.
    {
        const Vec3 cam = {1000.0f, 0.0f, 0.0f};
        const Vec3 origin = {0.0f, 0.0f, 0.0f};
        const int small = SelectLodLevel(cam, origin, 0.25f, 2);
        const int large = SelectLodLevel(cam, origin, 1.0f, 2);
        // Larger scale => larger thresholds => potentially lower (closer) LOD.
        expect(large <= small, "larger lod_scale gives LOD <= smaller scale");
    }

    // --- DirectionalIndexFromDelta ---
    expect(DirectionalIndexFromDelta({1, 0, 0}) == 2, "dx>0 -> EAST(2)");
    expect(DirectionalIndexFromDelta({-1, 0, 0}) == 3, "dx<0 -> WEST(3)");
    expect(DirectionalIndexFromDelta({0, 0, 1}) == 0, "dz>0 -> SOUTH(0)");
    expect(DirectionalIndexFromDelta({0, 0, -1}) == 1, "dz<0 -> NORTH(1)");

    if (failures == 0) {
        std::printf("lod_math: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "lod_math: %d failures\n", failures);
    return 1;
}
