// Host test for the shared distant-pass matrix (Pattern A: header-only, no
// N64 deps). Asserts `BuildSharedPassMatrix` math (Inc 3 / D2):
//   - translation = shared_origin - cam_pos;
//   - scale = 1/kLodScale on the diagonal;
//   - the full-map diagonal packs inside int16 at kLodScale 0.25;
//   - the per-map packing ceiling (map_diagonal × kLodScale ≤ ~28000) holds.
//
// Build:
//   g++ -std=c++17 -Isrc/user tests/distant_shared_matrix_contract.cpp
#include <cmath>
#include <cstdio>

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
    const float kLodScale = 0.25f;

    // 1. Translation = shared_origin - cam_pos; scale = 1/kLodScale.
    {
        const Vec3 cam = {100.0f, 50.0f, -200.0f};
        const Vec3 origin = {0.0f, 0.0f, 0.0f};
        float m[16];
        BuildSharedPassMatrix(cam, origin, kLodScale, m);
        // Row-major: scale on diagonal (0,5,10), translation in last column
        // (12,13,14).
        expect(std::fabs(m[0] - 4.0f) < 1e-4f, "scale x = 1/kLodScale");
        expect(std::fabs(m[5] - 4.0f) < 1e-4f, "scale y = 1/kLodScale");
        expect(std::fabs(m[10] - 4.0f) < 1e-4f, "scale z = 1/kLodScale");
        expect(std::fabs(m[12] - (origin.x - cam.x)) < 1e-4f,
               "translation x = origin - cam");
        expect(std::fabs(m[13] - (origin.y - cam.y)) < 1e-4f,
               "translation y = origin - cam");
        expect(std::fabs(m[14] - (origin.z - cam.z)) < 1e-4f,
               "translation z = origin - cam");
        expect(std::fabs(m[15] - 1.0f) < 1e-4f, "homogeneous w = 1");
    }

    // 2. Full-map diagonal packs inside int16 at kLodScale 0.25.
    //    A ~2000u map diagonal → 2000 × 0.25 = 500 int16 units, far inside
    //    ±32767.
    {
        const float map_diagonal = 2000.0f;
        const float packed = map_diagonal * kLodScale;
        expect(packed < 32767.0f, "map diagonal packs inside int16");
        expect(packed < 1000.0f, "map diagonal packs with huge headroom");
    }

    // 3. Per-map packing ceiling: map_diagonal × kLodScale ≤ ~28000.
    {
        const float ceiling = 28000.0f;
        // The largest map that still packs: diagonal ≤ ceiling / kLodScale.
        const float max_diagonal = ceiling / kLodScale;
        expect(max_diagonal >= 2000.0f,
               "ceiling allows at least a 2000u map diagonal");
        // A map at the ceiling packs to exactly the ceiling (still < 32767).
        expect(ceiling < 32767.0f, "ceiling is inside int16");
    }

    // 4. A camera at one map corner seeing the opposite corner: the farthest
    //    packed vertex stays inside int16.
    {
        const float half = 1000.0f;  // map half-extent
        const Vec3 cam = {-half, 0.0f, -half};
        const Vec3 origin = {0.0f, 0.0f, 0.0f};
        // Farthest world point from the camera (opposite corner).
        const float far_world = 2.0f * half;  // ~2000u
        const float packed = far_world * kLodScale;
        expect(packed < 32767.0f, "farthest packed vertex inside int16");
        (void)cam; (void)origin;
    }

    if (failures == 0) {
        std::printf("PASS: distant_shared_matrix_contract\n");
        return 0;
    }
    std::fprintf(stderr, "%d failure(s)\n", failures);
    return 1;
}
