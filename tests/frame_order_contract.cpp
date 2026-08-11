// Host test for the two-pass frame order (Pattern A: header-only, no N64
// deps). Asserts:
//   - `OrderedFrameStages` produces `[Distant, LowPriority, HighPriority,
//     Present]` in that exact order (arch.md §21).
//   - `FrameStageName` maps each stage to a stable, distinct name.
//   - `BuildPassCameras` derives both pass cameras from one world-space
//     camera (so the distant + near cannot drift).
//
// Build:
//   g++ -std=c++17 -Isrc/user tests/frame_order_contract.cpp
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "gameplay/render/open_world_renderer.hpp"

using namespace madeline_cube;

static int failures = 0;

static void expect(bool cond, const char* what) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

int main() {
    // OrderedFrameStages must emit exactly 4 entries in the documented order.
    FrameStage stages[4];
    OrderedFrameStages(stages);
    expect(stages[0] == FrameStage::Distant, "stage 0 is Distant");
    expect(stages[1] == FrameStage::LowPriority, "stage 1 is LowPriority");
    expect(stages[2] == FrameStage::HighPriority, "stage 2 is HighPriority");
    expect(stages[3] == FrameStage::Present, "stage 3 is Present");

    // The four enum values are distinct.
    expect(FrameStage::Distant != FrameStage::LowPriority, "Distant != LowPriority");
    expect(FrameStage::LowPriority != FrameStage::HighPriority, "LowPriority != HighPriority");
    expect(FrameStage::HighPriority != FrameStage::Present, "HighPriority != Present");

    // FrameStageName maps each stage to a stable, distinct string.
    const char* names[4] = {
        FrameStageName(FrameStage::Distant),
        FrameStageName(FrameStage::LowPriority),
        FrameStageName(FrameStage::HighPriority),
        FrameStageName(FrameStage::Present),
    };
    for (int i = 0; i < 4; ++i) {
        for (int j = i + 1; j < 4; ++j) {
            expect(std::strcmp(names[i], names[j]) != 0, "stage names distinct");
        }
    }
    expect(std::strcmp(FrameStageName(FrameStage::Distant), "distant") == 0,
           "distant stage name is 'distant'");
    expect(std::strcmp(FrameStageName(FrameStage::HighPriority), "high_priority") == 0,
           "high_priority stage name is 'high_priority'");

    // BuildPassCameras derives both passes from a single world-space camera.
    const Vec3 cam_pos = {1000.0f, 50.0f, -300.0f};
    const Vec3 cam_tgt = {1024.0f, 50.0f, -300.0f};
    const float fov = 60.0f, near_p = 20.0f, far_p = 800.0f;
    const float tile_size = 240.0f, lod_scale = 0.25f;

    const PassCameras pc =
        BuildPassCameras(cam_pos, cam_tgt, fov, near_p, far_p, tile_size, lod_scale);

    // Both passes share the exact same camera position.
    expect(pc.near_cam.pos.x == pc.distant_cam.pos.x &&
           pc.near_cam.pos.y == pc.distant_cam.pos.y &&
           pc.near_cam.pos.z == pc.distant_cam.pos.z,
           "near + distant share camera position");
    expect(pc.near_cam.target.x == pc.distant_cam.target.x &&
           pc.near_cam.target.z == pc.distant_cam.target.z,
           "near + distant share camera target");

    // The near camera keeps the gameplay clip planes; the distant camera uses
    // the compressed range (distinct far). This is the whole point of the
    // two-pass design — the far plane doesn't have to match.
    expect(pc.near_cam.far == far_p, "near camera far is the gameplay far plane");
    expect(pc.distant_cam.far != far_p, "distant camera far differs from near far");

    if (failures == 0) {
        std::printf("frame_order_contract: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "frame_order_contract: %d failures\n", failures);
    return 1;
}
