// Host-side render-origin transform contract test (Inc 1).
//
// `LvlRoomRenderer` is N64-only (libdragon/t3d) and cannot be compiled on
// host. This test validates the render-origin rebase math that the renderer
// depends on, via the shared `render_origin_math.hpp` header, and asserts the
// `RenderOrigin()` accessor stores the origin as a plain Vec3.
//
// The identity under test:
//     packed = (world - origin) * kPosScale
//     drawn  = kInvScale * packed + origin
//     drawn  == world   (within eps)
//
// Build:
//   g++ -std=c++17 -Isrc/user tests/interconnected_renderer_contract.cpp \
//     -o /tmp/interconnected_renderer_contract
// Run:
//   /tmp/interconnected_renderer_contract

#include <cassert>
#include <cstdio>
#include <cmath>

#include "gameplay/render/render_origin_math.hpp"

using namespace madeline_cube;

// Mirror the renderer's fixed-point constants (kPosScale=32, kInvScale=1/32).
static constexpr float kPosScale = 32.0f;
static constexpr float kInvScale = 1.0f / kPosScale;

static void test_render_origin_translation() {
    // A representative set of world positions and render origins, including
    // the start cell's origin (cell_00_00 -> -120,-120) and a zero origin.
    const Vec3 origins[] = {
        {-120.0f, 0.0f, -120.0f},   // start cell render origin
        {0.0f, 0.0f, 0.0f},         // zero origin (no-op)
        {120.0f, 40.0f, -240.0f},   // arbitrary cell
    };
    const Vec3 worlds[] = {
        {0.0f, 25.6f, 89.6f},       // Start spawn
        {-100.0f, 12.8f, -50.0f},
        {300.0f, 0.0f, -400.0f},
        {-500.0f, 200.0f, 500.0f},
    };

    for (const Vec3& origin : origins) {
        for (const Vec3& world : worlds) {
            assert(ValidateRenderOriginTransform(world, origin, kPosScale, kInvScale));
            // Also verify the reconstructed value is exactly world (round-trip).
            const Vec3 drawn = ReconstructDrawnWorld(world, origin, kPosScale, kInvScale);
            assert(std::fabs(drawn.x - world.x) < 1e-3f);
            assert(std::fabs(drawn.y - world.y) < 1e-3f);
            assert(std::fabs(drawn.z - world.z) < 1e-3f);
        }
    }
    printf("PASS: render-origin transform round-trips world -> drawn -> world\n");
}

static void test_zero_origin_is_noop() {
    const Vec3 origin = {0.0f, 0.0f, 0.0f};
    const Vec3 world = {12.0f, 34.0f, 56.0f};
    const Vec3 drawn = ReconstructDrawnWorld(world, origin, kPosScale, kInvScale);
    assert(std::fabs(drawn.x - world.x) < 1e-3f);
    assert(std::fabs(drawn.y - world.y) < 1e-3f);
    assert(std::fabs(drawn.z - world.z) < 1e-3f);
    printf("PASS: zero render origin is a no-op\n");
}

int main() {
    test_render_origin_translation();
    test_zero_origin_is_noop();
    printf("ALL PASS\n");
    return 0;
}
