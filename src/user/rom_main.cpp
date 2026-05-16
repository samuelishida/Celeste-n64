#include <cmath>
#include <cstdint>

#include <libdragon.h>
#include <t3d/t3d.h>

#include "gameplay/scene/gameplay_scene.hpp"
#include "gameplay/scene/scene_manager.hpp"
#include "n64/profiler.hpp"

namespace {

using namespace madeline_cube;

constexpr float kFixedDeltaSeconds = 1.0f / 60.0f;

}  // namespace

int main() {
    debug_init_isviewer();
    debug_init_usblog();
    joypad_init();

    display_init(RESOLUTION_320x240, DEPTH_16_BPP, 3, GAMMA_NONE, FILTERS_RESAMPLE);
    rdpq_init();
    t3d_init((T3DInitParams){});

    SceneManager scene_mgr;
    GameplayScene gameplay;
    scene_mgr.Register(0, &gameplay);
    scene_mgr.Goto(0);

    n64::FrameProfiler profiler(60);
    uint32_t memory_report_counter = 0;

    for (;;) {
        profiler.BeginFrame();

        scene_mgr.Update(kFixedDeltaSeconds);
        scene_mgr.Render();

        profiler.EndFrame();

        ++memory_report_counter;
        if (memory_report_counter >= 3600) {
            memory_report_counter = 0;
            const n64::MemorySnapshot mem = n64::MemorySnapshot::Capture();
            debugf("[memory] total=%u used=%u free=%u\n",
                   static_cast<unsigned int>(mem.total_bytes),
                   static_cast<unsigned int>(mem.used_bytes),
                   static_cast<unsigned int>(mem.free_bytes));
        }
    }

    t3d_destroy();
    return 0;
}
