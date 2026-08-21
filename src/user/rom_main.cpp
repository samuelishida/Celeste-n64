#include <cmath>
#include <cstdint>

#include <libdragon.h>
#include <t3d/t3d.h>

#include "gameplay/scene/gameplay_scene.hpp"
#include "gameplay/scene/scene_manager.hpp"
#include "gameplay/scene/title_scene.hpp"
#include "n64/profiler.hpp"

namespace {

using namespace madeline_cube;

}  // namespace

int main() {
    debug_init_isviewer();
    debug_init_usblog();
    joypad_init();

    // Target hardware is the N64 with the Expansion Pak (8 MB RDRAM). The
    // open-world renderer's streaming/memory budget assumes the full 8 MB
    // heap; without the pak the ROM would run out of memory mid-map. Fail
    // early with a clear error screen instead of a crash if it is absent.
    assert_memory_expanded();

    display_init(RESOLUTION_320x240, DEPTH_16_BPP, 3, GAMMA_NONE, FILTERS_RESAMPLE);
    // Cap the displayed frame rate at 30 fps. The N64 VI runs at 60 Hz (NTSC);
    // without a limit the loop runs as fast as the RSP-bound workload allows,
    // which is 60+ in light scenes and drops as more cells stream in. Capping
    // at 30 gives a steady target and halves the per-frame RSP/RDRAM budget,
    // which is where the streaming/memory optimization plan targets.
    display_set_fps_limit(30.0f);
    rdpq_init();
    t3d_init((T3DInitParams){});

    if (dfs_init(DFS_DEFAULT_LOCATION) == 0) {
        debugf("dfs_init: OK\n");
    } else {
        debugf("dfs_init: FAILED\n");
    }

    FILE* test_marker = fopen("rom:/test_marker.txt", "r");
    if (test_marker) {
        char buffer[32] = {0};
        fgets(buffer, sizeof(buffer), test_marker);
        debugf("test_marker contents: %s", buffer);
        fclose(test_marker);
    } else {
        debugf("test_marker: NOT FOUND\n");
    }

    SceneManager scene_mgr;
    GameplayScene gameplay;
    gameplay.SetMapPack("rom:/lvl/forsyken-city/forsyken-city.mappack");
    // TitleScene title(&gameplay);
    scene_mgr.Register(0, &gameplay);  // skip title, go straight to gameplay
    // scene_mgr.Register(1, &gameplay);
    scene_mgr.Goto(0);

    n64::FrameProfiler profiler(60);
    // Silent: the consolidated report block below prints the whole-frame avg
    // (Inc 1 / instrumentation) so exactly ONE `[profiler] avg frame time` line
    // appears per 60 frames. The profiler still accumulates + refreshes
    // last_average_ms() at the interval; only the debugf self-print is gated.
    profiler.SetSilent(true);
    uint32_t memory_report_counter = 0;
    uint32_t counter_report_counter = 0;

    for (;;) {
        profiler.BeginFrame();

        // Drive the fixed-step simulation with REAL elapsed time, not a
        // constant 1/60. With the 30 fps cap the loop runs ~30 Hz, so a
        // constant 1/60 delta would make the game play at half speed.
        // display_get_delta_time() returns the time since the last displayed
        // frame (~1/30 s under the cap); the FixedStepAccumulator inside
        // GameplayScene::Update converts that into the right number of 60 Hz
        // physics ticks, keeping the simulation in real time at any frame rate.
        scene_mgr.Update(display_get_delta_time());
        scene_mgr.Render();

        // RSPQ-block-render Inc 3 / D8 (async-RSP sync): wait for the RSP to
        // finish the frame's commands before the next Update rewrites the
        // single-buffered matrices the RSP DMAs at command-execution time
        // (viewport _matCameraFP/_matProjFP, per-cell matrix_fp_, model
        // matrices). With RSPQ blocks the CPU emits a frame in ~0.1 ms and
        // would otherwise race 2+ frames ahead of the RSP (ring-buffer bound),
        // so the RSP read torn matrices -> cells twitched and split. The wait
        // is nearly free on real HW (the RSP work must happen anyway) and is
        // the same synchronization rule tiny3d documents for buffered
        // viewports.
        rspq_wait();

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

        // Inc 1 / D6: print the per-frame render draw counters at the same
        // 60-frame cadence as the profiler report so each pass's cost is
        // validated on device with hard numbers. All report sources read at the
        // same point after 60 frames, so they describe the same window. The
        // LOCAL profiler reports the whole-frame avg; the renderer's profiler
        // is SILENT (Inc 1) — rom_main is the single [profiler]-family report
        // path, so exactly ONE `[profiler] avg frame time` line prints.
        ++counter_report_counter;
        if (counter_report_counter >= 60) {
            counter_report_counter = 0;
            const madeline_cube::RenderCounters& c = gameplay.GetRenderCounters();

            // Whole-frame avg from the local profiler (unchanged).
            debugf("[profiler] avg frame time over 60 frames: %.3f ms (%.1f fps)\n",
                   static_cast<double>(profiler.last_average_ms()),
                   profiler.last_average_ms() > 0.0f
                       ? static_cast<double>(1000.0f / profiler.last_average_ms())
                       : 0.0);

            // Per-phase ms from the renderer's profiler. These are the
            // remaining per-pass costs after removing the z-split distant pass.
            const n64::FrameProfiler& rp = gameplay.Profiler();
            debugf("[render-phases] high_priority=%.3f streaming=%.3f ms\n",
                   static_cast<double>(rp.phase_average_ms(n64::FrameProfiler::kPhaseHighPriority)),
                   static_cast<double>(rp.phase_average_ms(n64::FrameProfiler::kPhaseStreaming)));

            // Draw counters for the single near pass.
            debugf("[counters] near_batches=%u texture_uploads=%u "
                   "vert_loads=%u syncs=%u\n",
                   static_cast<unsigned int>(c.near_batches),
                   static_cast<unsigned int>(c.texture_uploads),
                   static_cast<unsigned int>(c.vert_loads),
                   static_cast<unsigned int>(c.syncs));
        }
    }

    // Unreachable on N64 hardware; kept for host-testing exit paths.
    t3d_destroy();
    return 0;
}
