#include "gameplay/render/open_world_renderer.hpp"

#include <t3d/t3d.h>

#include "gameplay/render/skybox.hpp"
#include "gameplay/render/tile_streamer.hpp"
#include "gameplay/world/mappack_loader.hpp"

namespace madeline_cube {

OpenWorldRenderer::OpenWorldRenderer()
    : tile_streamer_(new TileStreamer()),
      skybox_(new Skybox()),
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
    delete skybox_;
    delete tile_streamer_;
}

void OpenWorldRenderer::RenderHighPriority(const CameraDesc& cam) {
    tile_streamer_->DrawHighPriority(cam);
}

void OpenWorldRenderer::Render(const CameraDesc& cam) {
    // Single near pass: draw the skybox first, then the resident ring.
    // The viewport is attached once per frame by the caller; no mid-frame
    // projection switching.
    skybox_->Draw(cam);

    profiler_.BeginPhase(n64::FrameProfiler::kPhaseHighPriority);
    RenderHighPriority(cam);
    profiler_.EndPhase(n64::FrameProfiler::kPhaseHighPriority);
}

void OpenWorldRenderer::BeginFrame() {
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

void OpenWorldRenderer::UpdateCamera(const CameraDesc& near_cam) {
    tile_streamer_->UpdateCamera(near_cam);
}

void OpenWorldRenderer::SetMaterialCatalog(const MaterialCatalog* catalog) {
    tile_streamer_->SetMaterialCatalog(catalog);
}

}  // namespace madeline_cube
