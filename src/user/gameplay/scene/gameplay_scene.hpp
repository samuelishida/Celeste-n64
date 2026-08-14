#pragma once

#include "gameplay/render/distant_world_renderer.hpp"  // DistantCellStat (Inc 3)
#include "gameplay/render/open_world_renderer.hpp"  // RenderCounters (Inc 1)
#include "n64/profiler.hpp"                        // FrameProfiler (Inc 1)
#include "gameplay/scene/scene.hpp"

namespace madeline_cube {

// Encapsulates the current gameplay loop (player, island, collectible, camera).
class GameplayScene : public Scene {
public:
    // Called by TitleScene before transitioning to set which level to load.
    void SetLevel(const char* lvl_path, const char* level_name);

    // Set the map-pack manifest to boot as a multi-room map. When set
    // (before Init), the scene boots the map-pack's start room and streams
    // chunks across boundaries. When null, the legacy single-room path
    // (SetLevel) is used.
    void SetMapPack(const char* mappack_path);

    void Init() override;
    void Shutdown() override;
    void Update(float delta_seconds) override;
    void Render() override;

    // Per-frame render draw counters (Inc 1 / D6). Forwarded from the
    // two-pass orchestrator so the reporting profiler (owned by rom_main.cpp)
    // can print them. Named `GetRenderCounters` (not `RenderCounters`) to
    // avoid collision with the `RenderCounters` type name.
    const RenderCounters& GetRenderCounters() const;

    // The renderer's per-phase profiler (Inc 1 / instrumentation). Forwarded
    // so rom_main can print phase_average_ms(). May be a zeroed fallback if
    // the scene is not yet initialized.
    const n64::FrameProfiler& Profiler() const;

    // The distant pass's per-cell cost capture (Inc 3 / instrumentation).
    // `*count` is set to the number of drawn cells this frame (≤ 64); returns
    // nullptr with *count=0 if the scene is not initialized.
    const DistantWorldRenderer::DistantCellStat* GetDistantCellStats(int* count) const;

private:
    struct Impl;
    Impl* impl_ = nullptr;

    const char* lvl_path_  = "rom:/lvl/1-1.lvl";
    const char* level_name_ = "1-1";
    const char* mappack_path_pending_ = nullptr;  // set by SetMapPack before Init
};

}  // namespace madeline_cube
