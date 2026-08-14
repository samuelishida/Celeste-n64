// Host test for per-direction distant mesh selection (Pattern A: header-only,
// no N64 deps). Asserts (Inc 4 / compressed-LOD):
//   (a) DirectionalMeshIndex picks the correct slot for the camera at N/S/E/W;
//   (b) uses the camera-facing rule when within `close_threshold` of the cell
//       center (Lambert §12).
//
// Build:
//   g++ -std=c++17 -Isrc/user tests/directional_lod_contract.cpp
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
    const Vec3 origin = {0.0f, 0.0f, 0.0f};
    const float close = 120.0f;

    // Camera far from the cell center: direction = cell→camera delta.
    // 0 = +Z (south), 1 = -Z (north), 2 = +X (east), 3 = -X (west).
    expect(DirectionalMeshIndex({0, 0, 500}, origin, {0, 0, 1}, close) == 0,
           "camera +Z -> south (0)");
    expect(DirectionalMeshIndex({0, 0, -500}, origin, {0, 0, -1}, close) == 1,
           "camera -Z -> north (1)");
    expect(DirectionalMeshIndex({500, 0, 0}, origin, {1, 0, 0}, close) == 2,
           "camera +X -> east (2)");
    expect(DirectionalMeshIndex({-500, 0, 0}, origin, {-1, 0, 0}, close) == 3,
           "camera -X -> west (3)");

    // Diagonal: dominant axis wins.
    expect(DirectionalMeshIndex({500, 0, 300}, origin, {1, 0, 0}, close) == 2,
           "camera +X+Z dominant X -> east (2)");
    expect(DirectionalMeshIndex({300, 0, 500}, origin, {0, 0, 1}, close) == 0,
           "camera +X+Z dominant Z -> south (0)");

    // Camera near the cell center: use the camera's own facing.
    expect(DirectionalMeshIndex({10, 0, 10}, origin, {0, 0, 1}, close) == 0,
           "near center facing +Z -> south (0)");
    expect(DirectionalMeshIndex({-10, 0, -10}, origin, {-1, 0, 0}, close) == 3,
           "near center facing -X -> west (3)");

    if (failures) {
        std::fprintf(stderr, "%d FAILURES\n", failures);
        return 1;
    }
    std::printf("ALL PASS\n");
    return 0;
}
