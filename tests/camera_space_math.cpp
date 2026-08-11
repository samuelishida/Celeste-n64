// Host test for camera-relative transform math (Pattern A: header-only, no
// N64 deps).
//
// Build:
//   g++ -std=c++17 -Isrc/user tests/camera_space_math.cpp
#include <cstdio>
#include <cstdlib>
#include <cmath>

#include "gameplay/render/camera_space_math.hpp"

using namespace madeline_cube;

static int failures = 0;

static void expect(bool cond, const char* what) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

int main() {
    const float kPosScale = 32.0f;
    // Limit delta that still fits: 32767/32 ≈ 1023.97 world units.
    const float kLimit = 32767.0f / kPosScale;  // ~1023.97
    const Vec3 camera = {1000.0f, 50.0f, -300.0f};

    // Round-trip is exact for a set of representative positions.
    const Vec3 points[] = {
        {0.0f, 0.0f, 0.0f},                // far from camera
        {1000.0f, 50.0f, -300.0f},         // exactly on camera
        {1024.0f, 50.0f, -300.0f},         // +24 x (delta < limit, fits)
        {1000.0f, 50.0f, -300.0f + kLimit}, // delta z = +limit, fits
        {-1024.0f, 50.0f, -300.0f},        // delta x = -2024 (overflow)
        {1000.0f, 50.0f, 308.0f},          // delta z = +608 (fits)
        {512.0f, -100.0f, 900.0f},         // arbitrary far point
    };
    for (const Vec3& p : points) {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "roundtrip (%.1f,%.1f,%.1f)",
                      p.x, p.y, p.z);
        expect(ValidateCameraSpaceRoundTrip(p, camera), buf);
    }

    // PackedFitsInt16: the delta (world - camera) must stay within
    // ±kLimit at kPosScale=32 on every axis.
    expect(PackedFitsInt16({1000.0f, 50.0f, -300.0f}, camera, kPosScale),
           "on-camera fits int16");
    expect(PackedFitsInt16({1024.0f, 50.0f, -300.0f}, camera, kPosScale),
           "delta +24 fits int16");
    expect(PackedFitsInt16({1000.0f, 50.0f, -300.0f + kLimit}, camera, kPosScale),
           "delta +limit fits int16");
    expect(PackedFitsInt16({1000.0f, 50.0f, -300.0f - kLimit}, camera, kPosScale),
           "delta -limit fits int16");
    expect(!PackedFitsInt16({1000.0f, 50.0f, -300.0f + kLimit + 1.0f},
                            camera, kPosScale),
           "delta past +limit does NOT fit int16");
    expect(!PackedFitsInt16({-1024.0f, 50.0f, -300.0f}, camera, kPosScale),
           "delta -2024 does NOT fit int16");
    expect(PackedFitsInt16({2000.0f, 50.0f, -300.0f}, camera, kPosScale),
           "delta +1000 fits int16");

    // Cell-boundary extreme: camera at a cell-corner and a world point at the
    // diagonal opposite corner. The delta is the full cell diagonal and must
    // still fit if it is within ±1024 units.
    const Vec3 corner_cam = {240.0f, 0.0f, 240.0f};
    const Vec3 opposite = {0.0f, 0.0f, 0.0f};
    expect(PackedFitsInt16(opposite, corner_cam, kPosScale),
           "cell-corner-to-opposite fits int16");

    if (failures == 0) {
        std::printf("camera_space_math: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "camera_space_math: %d failures\n", failures);
    return 1;
}
