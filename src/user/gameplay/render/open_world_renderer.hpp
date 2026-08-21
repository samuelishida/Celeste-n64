#pragma once

#include "gameplay/math_types.hpp"
#include "gameplay/render/pass_camera_math.hpp"
#include "n64/frame_arena.hpp"
#include "n64/profiler.hpp"

namespace madeline_cube {

// Per-frame draw counters. Reset each frame in `BeginFrame` and consumed by
// the profiler report + device walk. Host-safe — plain integers, no N64 types.
struct RenderCounters {
    uint32_t near_batches = 0;     // batches drawn in the near pass
    uint32_t texture_uploads = 0;  // rdpq_sprite_upload calls (near pass)
    uint32_t vert_loads = 0;       // t3d_vert_load calls (near pass)
    uint32_t syncs = 0;            // t3d_tri_sync calls (near pass)
};

// N64-only renderer types, forward-declared so this header stays host-safe.
class TileStreamer;
class Skybox;

// Device-only render orchestrator. Drives a single near pass plus skybox.
class OpenWorldRenderer {
public:
    OpenWorldRenderer();
    ~OpenWorldRenderer();
    OpenWorldRenderer(const OpenWorldRenderer&) = delete;
    OpenWorldRenderer& operator=(const OpenWorldRenderer&) = delete;

    // Full frame: skybox, then single near pass.
    void Render(const CameraDesc& cam);

    // Resident-pool management.
    void SetCenter(const class MapSpecV2& spec, const class V2RoomSpec& center,
                   const Vec3& camera_dir, const char* build_dir);
    void SetCameraPosition(const Vec3& camera_pos);

    // Per-frame resident visibility update. The streamer resolves which
    // resident cells are inside the camera cone and draws only those.
    void UpdateCamera(const CameraDesc& cam);

    // Set the material catalog for the textured near pass. The catalog is
    // owned by the caller.
    void SetMaterialCatalog(const class MaterialCatalog* catalog);

    // Set the viewport pointer for host tests / legacy callers. The viewport
    // is attached once by the caller before Render(). Null disables any attach
    // here (host tests).
    void SetViewport(void* viewport) { viewport_ = viewport; }

    // Reset the frame-scoped arena at the start of each frame. Call at the TOP
    // of GameplayScene::Update so the streaming phase (emitted inside SetCenter
    // during transitions) is NOT wiped by the BeginFrame reset.
    void BeginFrame();

    // Close the per-frame profiler span. Call at the END of GameplayScene::
    // Render, after all phases, so the whole Update+Render span is measured.
    void EndFrame();

    // The frame-scoped arena for transient per-frame allocations.
    n64::FrameArena& Arena() { return arena_; }

    // Per-phase profiler. Reports per-pass timing.
    n64::FrameProfiler& Profiler() { return profiler_; }

    // The per-frame draw counters. Reset in BeginFrame; filled by the near
    // pass. Read by the profiler report / device walk.
    const RenderCounters& Counters() const { return counters_; }

private:
    void RenderHighPriority(const CameraDesc& cam);

    TileStreamer* tile_streamer_ = nullptr;
    Skybox* skybox_ = nullptr;
    void* viewport_ = nullptr;
    Vec3 camera_pos_ = {0.0f, 0.0f, 0.0f};
    n64::FrameArena arena_;
    n64::FrameProfiler profiler_;
    RenderCounters counters_;
};

}  // namespace madeline_cube
