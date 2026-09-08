#pragma once

#include "gameplay/render/open_world_renderer.hpp"  // RenderCounters
#include "n64/profiler.hpp"                         // FrameProfiler
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

    // Per-frame render draw counters. Forwarded from the near-pass orchestrator
    // so the reporting profiler (owned by rom_main.cpp) can print them.
    const RenderCounters& GetRenderCounters() const;

    // The renderer's per-phase profiler. Forwarded so rom_main can print
    // phase_average_ms(). May be a zeroed fallback if the scene is not yet
    // initialized.
    const n64::FrameProfiler& Profiler() const;

    // streaming-experience Inc 5: streaming-telemetry passthrough for the
    // [counters] report line (cells in the pending-load backlog / resident
    // depth). Returns 0 before the scene is initialized (the counters block
    // in rom_main only runs once the map has booted, so impl_ is live there).
    int PendingLoads() const;
    int ResidentLoads() const;

    // memory-opt Inc 1: memory-component buckets for the [memory] report.
    // Each returns 0 before the scene is initialized. Forwarded from the
    // open-world renderer / map runtime (see register sites in rom_main).
    size_t FrameArenaUsed() const;
    size_t StreamingStagingBytes() const;
    size_t StreamingResidentBytes() const;
    size_t LiveCollMeshBytes() const;
    size_t MapSpecBytes() const;
    // memory-opt Inc 4: renderer-pool high-water + frame-arena peak buckets.
    size_t PoolHighWater() const;
    size_t FrameArenaPeak() const;

private:
    struct Impl;
    Impl* impl_ = nullptr;

    const char* lvl_path_  = "rom:/lvl/first-room.lvl";
    const char* level_name_ = "first-room";
    const char* mappack_path_pending_ = nullptr;  // set by SetMapPack before Init
};

}  // namespace madeline_cube
