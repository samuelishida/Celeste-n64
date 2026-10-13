#pragma once

#include "gameplay/math_types.hpp"
#include "gameplay/render/fog_math.hpp"
#include "gameplay/render/pass_camera_math.hpp"
#include "n64/frame_arena.hpp"
#include "n64/profiler.hpp"

namespace madeline_cube {

// Per-frame draw counters. Reset each frame in `BeginFrame` and consumed by
// the profiler report + device walk. Host-safe — plain integers, no N64 types.
struct RenderCounters {
    uint32_t near_batches = 0;     // batches drawn in the near pass
    uint32_t near_material_groups = 0; // per-cell material groups replayed
    uint32_t distant_cells = 0;    // visible DLOD cells drawn in horizon pass
    uint32_t distant_faces = 0;    // visible DLOD triangles submitted
    uint32_t distant_batches = 0;  // material runs/fallback batches in DLOD
    uint32_t distant_vert_loads = 0; // t3d_vert_load calls in the DLOD pass
    uint32_t distant_syncs = 0;    // t3d_tri_sync calls in the DLOD pass
    uint32_t texture_uploads = 0;  // rdpq_sprite_upload calls (near pass)
    uint32_t vert_loads = 0;       // t3d_vert_load calls (near pass)
    uint32_t syncs = 0;            // t3d_tri_sync calls (near pass)
};

// N64-only renderer types, forward-declared so this header stays host-safe.
class TileStreamer;
class Skybox;
class DistantTierRenderer;  // render-distance Inc 4
class DynamicSolidGeometry;
class DynamicSolidRenderer;
class MaterialCatalog;

// Device-only render orchestrator. Draws horizon DLOD, then detailed near ring.
class OpenWorldRenderer {
public:
    OpenWorldRenderer();
    ~OpenWorldRenderer();
    OpenWorldRenderer(const OpenWorldRenderer&) = delete;
    OpenWorldRenderer& operator=(const OpenWorldRenderer&) = delete;

    // Full frame: caller attached sky viewport; renderer attaches distant,
    // clears depth, then attaches near viewport for detailed ring.
    void Render(const CameraDesc& near_cam, const CameraDesc& distant_cam,
                const Vec3* player_position = nullptr);

    // Resident-pool management.
    void SetCenter(const class MapSpecV2& spec, const class V2RoomSpec& center,
                   const Vec3& camera_dir, const char* build_dir);
    void SetCameraPosition(const Vec3& camera_pos);

    // Per-frame resident draw-mask update. Loaded residents stay drawable;
    // `dt` is the frame delta in seconds (ages the optional reveal state).
    void UpdateCamera(const CameraDesc& cam, float dt = 1.0f / 30.0f);

    // Set the material catalog for the textured near pass. The catalog is
    // owned by the caller.
    void SetMaterialCatalog(const class MaterialCatalog* catalog);

    // Attach map-owned actor-local geometry. Prepare fails closed on unsafe
    // packed ranges; nullptr detaches before MapRuntime releases its storage.
    bool SetDynamicSolids(const DynamicSolidGeometry* geometry);

    // Set independently buffered distant and near viewports. Caller attaches
    // the sky viewport before Render(); each viewport is attached once/frame.
    void SetViewports(void* near_viewport, void* distant_viewport) {
        near_viewport_ = near_viewport;
        distant_viewport_ = distant_viewport;
    }
    void SetViewport(void* near_viewport) { near_viewport_ = near_viewport; }

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

    // Per-frame draw counters. Reset in BeginFrame; filled by near and DLOD.
    const RenderCounters& Counters() const { return counters_; }

    // streaming-experience Inc 5: streaming telemetry for the [counters]
    // report line — cells still in the pending-load backlog (queued/in-flight)
    // and resident pool depth. `pending=0` = full ring READY.
    int PendingLoads() const;
    int ResidentLoads() const;

    // render-distance Inc 4: load the whole-map horizon tier (all `.dlod`
    // cells) once at boot. `build_dir` is null on device.
    void LoadDistantTier(const MapSpecV2& spec, const char* build_dir);

    // Inc 6: configure the horizon fog for the distant tier. Forwarded to the
    // distant renderer; applied around its draw and torn down before the near.
    void SetFog(const FogParams& fog);

    // memory-opt Inc 1: streaming memory components for the [memory] report
    // buckets (delegated to the tile streamer). 0 before the streamer exists.
    size_t StreamingStagingBytes() const;
    size_t StreamingResidentBytes() const;

    // memory-opt Inc 4: renderer-pool high-water (live renderer objects) and
    // frame-arena peak (high-water of Used() since boot).
    size_t PoolHighWater() const;
    size_t FrameArenaPeak() const;

private:
    void RenderHighPriority(const CameraDesc& cam);

    TileStreamer* tile_streamer_ = nullptr;
    Skybox* skybox_ = nullptr;
    DistantTierRenderer* distant_tier_ = nullptr;  // render-distance Inc 4
    DynamicSolidRenderer* dynamic_solid_renderer_ = nullptr;
    const DynamicSolidGeometry* dynamic_solids_ = nullptr;
    const MaterialCatalog* material_catalog_ = nullptr;
    void* near_viewport_ = nullptr;
    void* distant_viewport_ = nullptr;
    Vec3 camera_pos_ = {0.0f, 0.0f, 0.0f};
    n64::FrameArena arena_;
    n64::FrameProfiler profiler_;
    RenderCounters counters_;
};

}  // namespace madeline_cube
