// Host test for the skybox transform contract (Pattern A: header-only, no
// N64 deps). Asserts the skybox transform is rotation-only (camera-relative
// translation zeroed, arch.md §29) so the sky stays stationary relative to
// the camera while terrain moves. The skybox transform must be independent of
// the camera position and target.
//
// Build:
//   g++ -std=c++17 -Isrc/user tests/skybox_transform.cpp
#include <cstdio>
#include <cstdlib>

#include "gameplay/render/skybox_math.hpp"

using namespace madeline_cube;

static int failures = 0;

static void expect(bool cond, const char* what) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

int main() {
    // The skybox transform is rotation-only: it must be valid (zero
    // translation) for any camera position/target.
    expect(ValidateSkyboxTransform({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}),
           "skybox valid at origin");
    expect(ValidateSkyboxTransform({1000.0f, 50.0f, -300.0f}, {1024.0f, 50.0f, -300.0f}),
           "skybox valid at a far camera");
    expect(ValidateSkyboxTransform({-500.0f, 200.0f, 800.0f}, {0.0f, 0.0f, 0.0f}),
           "skybox valid at a negative camera");

    // The skybox transform is independent of the camera target (rotation-only,
    // no look-at translation). Moving the target must not change the skybox's
    // validity (it stays rotation-only).
    expect(ValidateSkyboxTransform({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}) ==
           ValidateSkyboxTransform({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}),
           "skybox independent of camera target");

    if (failures == 0) {
        std::printf("skybox_transform: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "skybox_transform: %d failures\n", failures);
    return 1;
}
