#include "gameplay/render/open_world_renderer.hpp"

#include <t3d/t3d.h>
#include <rdpq.h>
#include <rdpq_mode.h>

#include "gameplay/render/distant_tier_renderer.hpp"  // render-distance Inc 4
#include "gameplay/world/dynamic_solid_geometry.hpp"
#include "gameplay/render/dynamic_solid_renderer.hpp"
#include "gameplay/render/material_catalog.hpp"
#include "gameplay/render/skybox.hpp"
#include "gameplay/render/tile_streamer.hpp"
#include "gameplay/world/mappack_loader.hpp"
#include "gameplay/debug_flags.hpp"  // kEnableDistantTier (Inc 4/5)

namespace madeline_cube {

OpenWorldRenderer::OpenWorldRenderer()
    : tile_streamer_(new TileStreamer()),
      skybox_(new Skybox()),
      distant_tier_(new DistantTierRenderer()),
      dynamic_solid_renderer_(new DynamicSolidRenderer()),
      profiler_(60) {
    skybox_->Init(nullptr);  // flat-colored dome (textured skybox is future work)
    // The renderer's profiler is SILENT: rom_main is the single report path and
    // reads phase_average_ms() every 60 frames. The 60-frame interval keeps the
    // averages fresh; only the debugf self-print is suppressed.
    profiler_.SetSilent(true);
    // Thread the per-frame counters + profiler into the near pass. Both are set
    // once in the ctor and live for the renderer's lifetime.
    tile_streamer_->SetCounters(&counters_);
    tile_streamer_->SetProfiler(&profiler_);
}

OpenWorldRenderer::~OpenWorldRenderer() {
    delete dynamic_solid_renderer_;
    delete distant_tier_;
    delete skybox_;
    delete tile_streamer_;
}

int OpenWorldRenderer::PendingLoads() const {
    return tile_streamer_ ? tile_streamer_->EnqueuedCount() : 0;
}

int OpenWorldRenderer::ResidentLoads() const {
    return tile_streamer_ ? tile_streamer_->ResidentCount() : 0;
}

size_t OpenWorldRenderer::StreamingStagingBytes() const {
    // memory-opt Inc 1: [memory] bucket — live staging pool bytes.
    return tile_streamer_ ? tile_streamer_->StagingBytes() : 0;
}

size_t OpenWorldRenderer::StreamingResidentBytes() const {
    // memory-opt Inc 1: [memory] bucket — live resident renderer objects.
    return tile_streamer_ ? tile_streamer_->ResidentRendererBytes() : 0;
}

size_t OpenWorldRenderer::PoolHighWater() const {
    // memory-opt Inc 4: [memory] bucket — renderer-pool high-water.
    return tile_streamer_ ? tile_streamer_->PoolHighWater() : 0;
}

size_t OpenWorldRenderer::FrameArenaPeak() const {
    // memory-opt Inc 4: [memory] bucket — frame-arena peak since boot.
    return arena_.PeakUsed();
}

void OpenWorldRenderer::RenderHighPriority(const CameraDesc& cam) {
    tile_streamer_->DrawHighPriority(cam);
}

void OpenWorldRenderer::Render(const CameraDesc& near_cam,
                               const CameraDesc& distant_cam,
                               const Vec3* player_position) {
    // Caller attached a dedicated sky viewport using near clip planes; the
    // sky cube must not be clipped by distant.near (>350u). All viewports have
    // independent matrix buffers, so no projection is rewritten in flight.
    skybox_->Draw(near_cam);
    if (kEnableDistantTier && distant_tier_ && distant_viewport_) {
        profiler_.BeginPhase(n64::FrameProfiler::kPhaseDistant);
        t3d_viewport_attach(static_cast<T3DViewport*>(distant_viewport_));
        t3d_state_set_drawflags(static_cast<T3DDrawFlags>(
            T3D_FLAG_SHADED | T3D_FLAG_DEPTH));
        rdpq_mode_mipmap(MIPMAP_NONE, 0);
        rdpq_mode_combiner(
            RDPQ_COMBINER1((PRIM,0,SHADE,0),(PRIM,0,SHADE,0)));
        distant_tier_->Draw(distant_cam, &counters_, *tile_streamer_);
        profiler_.EndPhase(n64::FrameProfiler::kPhaseDistant);
    }

    // Never compare Z values produced by different projections. Horizon is a
    // background fill; clear depth before drawing detailed near geometry.
    if (near_viewport_) {
        t3d_viewport_attach(static_cast<T3DViewport*>(near_viewport_));
        t3d_screen_clear_depth();
    }

    profiler_.BeginPhase(n64::FrameProfiler::kPhaseHighPriority);
    RenderHighPriority(near_cam);
    if (dynamic_solids_ && dynamic_solid_renderer_) {
        dynamic_solid_renderer_->Draw(*dynamic_solids_, material_catalog_, near_cam,
                                      &counters_, player_position);
    }
    profiler_.EndPhase(n64::FrameProfiler::kPhaseHighPriority);
}

void OpenWorldRenderer::LoadDistantTier(const MapSpecV2& spec,
                                        const char* build_dir) {
    if (distant_tier_) distant_tier_->LoadAll(spec, build_dir);
}

void OpenWorldRenderer::SetFog(const FogParams& fog) {
    if (distant_tier_) distant_tier_->SetFog(fog);
}

void OpenWorldRenderer::BeginFrame() {
    // streaming-experience Inc 1: pump the async cell-load queue at the top of
    // Update, BEFORE SetCenter issues new loads. Loads issued this frame are
    // pumped next frame (acceptable given the preload ring). The pump parses
    // at most one completed cell per frame, so the transition-frame spike is
    // spread across frames instead of landing all at once.
    tile_streamer_->PumpAsyncLoads();
    // Reset the frame-scoped arena at the start of each frame.
    arena_.Reset();
    // Reset the per-frame draw counters.
    counters_ = RenderCounters{};
    // Open the per-frame profiler span. Must run at the top of Update (before
    // any SetCenter/transition) so streaming ticks are not wiped by this reset.
    profiler_.BeginFrame();
}

void OpenWorldRenderer::EndFrame() {
    // Close the per-frame profiler span. Safe to call every frame; the report
    // fires at the 60-frame interval and is silent (rom_main prints it).
    profiler_.EndFrame();
}

void OpenWorldRenderer::SetCenter(const MapSpecV2& spec,
                                  const V2RoomSpec& center,
                                  const Vec3& camera_dir,
                                  const char* build_dir) {
    // streaming-experience Inc 2: pre-reserve the streaming heap budget at the
    // first SetCenter (the spec is not available in the ctor). Idempotent —
    // only reserves once. Sizes the async staging pool from the manifest's
    // largest cell so a whole-file DMA read of any cell fits.
    tile_streamer_->ReserveStreamingBudget(spec.MaxLvlSize());
    // Measure the chunk-transition load (near ring) under kPhaseStreaming so
    // transition hitches show in the profiler report. SetCenter is called from
    // Update (transitions/boot), inside the per-frame profiler span, so the
    // phase accumulates only on transition frames.
    profiler_.BeginPhase(n64::FrameProfiler::kPhaseStreaming);
    tile_streamer_->SetCenter(spec, center, camera_dir, build_dir);
    profiler_.EndPhase(n64::FrameProfiler::kPhaseStreaming);
}

void OpenWorldRenderer::SetCameraPosition(const Vec3& camera_pos) {
    camera_pos_ = camera_pos;
    tile_streamer_->SetCameraPosition(camera_pos);
}

void OpenWorldRenderer::UpdateCamera(const CameraDesc& near_cam, float dt) {
    tile_streamer_->UpdateCamera(near_cam, dt);
}

void OpenWorldRenderer::SetMaterialCatalog(const MaterialCatalog* catalog) {
    material_catalog_ = catalog;
    tile_streamer_->SetMaterialCatalog(catalog);
}

bool OpenWorldRenderer::SetDynamicSolids(const DynamicSolidGeometry* geometry) {
    if (!geometry) {
        if (dynamic_solid_renderer_) dynamic_solid_renderer_->Free();
        dynamic_solids_ = nullptr;
        return true;
    }
    if (!dynamic_solid_renderer_ || !dynamic_solid_renderer_->Prepare(*geometry)) {
        dynamic_solids_ = nullptr;
        return false;
    }
    dynamic_solids_ = geometry;
    return true;
}

}  // namespace madeline_cube
