#include "gameplay/render/open_world_renderer.hpp"

#include "gameplay/render/distant_world_renderer.hpp"
#include "gameplay/render/skybox.hpp"
#include "gameplay/render/tile_streamer.hpp"
#include "gameplay/world/mappack_loader.hpp"

namespace madeline_cube {

OpenWorldRenderer::OpenWorldRenderer()
    : tile_streamer_(new TileStreamer()),
      distant_(new DistantWorldRenderer()),
      skybox_(new Skybox()) {
    skybox_->Init(nullptr);  // flat-colored dome (textured skybox is future work)
    // Thread the per-frame counters + profiler into the passes (Inc 1 / D7) so
    // the room renderers can record draw counters and the texture-upload phase
    // is emitted. Both are set once in the ctor and live for the renderer's
    // lifetime.
    tile_streamer_->SetCounters(&counters_);
    tile_streamer_->SetProfiler(&profiler_);
    distant_->SetCounters(&counters_);
    // Inc 5 / D6: forward the frame-scoped arena so the distant render list
    // and the near-pass visible snapshot are allocated per-frame from it.
    tile_streamer_->SetArena(&arena_);
    distant_->SetArena(&arena_);
}

OpenWorldRenderer::~OpenWorldRenderer() {
    delete skybox_;
    delete distant_;
    delete tile_streamer_;
}

void OpenWorldRenderer::RenderDistant(const CameraDesc& cam) {
    // Inc 6: the skybox is drawn first (rotation-only transform), then the
    // distant cells with fog.
    skybox_->Draw(cam);
    distant_->UpdateCamera(cam.pos, cam);
    distant_->Render(cam);
}

void OpenWorldRenderer::RenderLowPriority(const CameraDesc& cam) {
    // Inc 2 stub: tile_streamer_ low-priority is a no-op. Inc 5 fills it in.
    tile_streamer_->DrawLowPriority(cam);
}

void OpenWorldRenderer::RenderHighPriority(const CameraDesc& cam) {
    tile_streamer_->DrawHighPriority(cam);
}

void OpenWorldRenderer::Render(const PassCameras& cams) {
    // arch.md §21 order: distant (Z off), low-priority (Z off), high-priority
    // (Z on). The skybox is prepended in Inc 6. Each pass is wrapped in a
    // profiler phase scope (Inc 7).
    profiler_.BeginPhase(n64::FrameProfiler::kPhaseDistant);
    RenderDistant(cams.distant_cam);
    profiler_.EndPhase(n64::FrameProfiler::kPhaseDistant);

    profiler_.BeginPhase(n64::FrameProfiler::kPhaseLowPriority);
    RenderLowPriority(cams.near_cam);
    profiler_.EndPhase(n64::FrameProfiler::kPhaseLowPriority);

    profiler_.BeginPhase(n64::FrameProfiler::kPhaseHighPriority);
    RenderHighPriority(cams.near_cam);
    profiler_.EndPhase(n64::FrameProfiler::kPhaseHighPriority);
}

void OpenWorldRenderer::BeginFrame() {
    // Reset the frame-scoped arena at the start of each frame (Inc 7).
    arena_.Reset();
    // Reset the per-frame draw counters (Inc 1 / D7).
    counters_ = RenderCounters{};
}

void OpenWorldRenderer::SetCenter(const MapSpecV2& spec,
                                  const V2RoomSpec& center,
                                  const char* build_dir) {
    // Inc 6 / D7: measure the chunk-transition load (near ring + distant LOD
    // table) under kPhaseStreaming so transition hitches show in the profiler
    // report. SetCenter is called from Update (transitions/boot), inside the
    // per-frame profiler span, so the phase accumulates only on transition
    // frames (≈0.000 otherwise) — exactly what the budget wants to catch.
    profiler_.BeginPhase(n64::FrameProfiler::kPhaseStreaming);
    tile_streamer_->SetCenter(spec, center, build_dir);
    // Load the distant LOD table once per map-pack (all cells, coarse).
    // Inc 4: the distant pass renders the horizon from these coarse meshes.
    if (distant_->EntryCount() == 0) {
        distant_->Load(spec, build_dir);
    }
    // Fan the camera position to the distant pass too (compressed rebase).
    distant_->SetCameraPosition(camera_pos_);
    profiler_.EndPhase(n64::FrameProfiler::kPhaseStreaming);
}

void OpenWorldRenderer::SetCameraPosition(const Vec3& camera_pos) {
    camera_pos_ = camera_pos;
    tile_streamer_->SetCameraPosition(camera_pos);
    distant_->SetCameraPosition(camera_pos);
}

void OpenWorldRenderer::UpdateCamera(const Vec3& camera_pos,
                                     const Mat4& inv_view_proj,
                                     float ground_y) {
    // Inc 4 / D4: forward the world-space inverse view-projection to the near
    // pass so it can resolve which residents are actually visible this frame.
    tile_streamer_->UpdateCamera(camera_pos, inv_view_proj, ground_y);
}

void OpenWorldRenderer::SetMaterialCatalog(const MaterialCatalog* catalog) {
    tile_streamer_->SetMaterialCatalog(catalog);
}

void OpenWorldRenderer::SetFog(const FogParams& fog) {
    distant_->SetFog(fog);
}

}  // namespace madeline_cube
