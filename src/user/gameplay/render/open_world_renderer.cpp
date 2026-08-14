#include "gameplay/render/open_world_renderer.hpp"

#include <t3d/t3d.h>

#include "gameplay/render/distant_world_renderer.hpp"
#include "gameplay/render/skybox.hpp"
#include "gameplay/render/tile_streamer.hpp"
#include "gameplay/world/mappack_loader.hpp"

namespace madeline_cube {

namespace {

// Attach a viewport with the given projection (camera-at-origin look_at).
// The camera-at-origin coupling is load-bearing: model matrices are
// camera-relative (LvlRoomRenderer::SetCameraPosition rebases by
// render_origin - camera_pos), so the view must ALSO be camera-at-origin —
// origin at zero, target offset by -camera — or geometry double-offsets and
// pops (see gameplay_scene.cpp:830-840). The switch order is mandatory:
// set_projection → look_at → attach (attach unconditionally emits both
// matrices; skipping look_at pushes the new projection with a stale camera).
void AttachCameraAtOriginViewport(T3DViewport* viewport, const CameraDesc& cam) {
    t3d_viewport_set_projection(viewport, T3D_DEG_TO_RAD(cam.fov_deg),
                                cam.near, cam.far);
    const T3DVec3 origin = {{0.0f, 0.0f, 0.0f}};
    const T3DVec3 target = {{cam.target.x - cam.pos.x,
                             cam.target.y - cam.pos.y,
                             cam.target.z - cam.pos.z}};
    const T3DVec3 up = {{0.0f, 1.0f, 0.0f}};
    t3d_viewport_look_at(viewport, &origin, &target, &up);
    t3d_viewport_attach(viewport);
}

}  // namespace

OpenWorldRenderer::OpenWorldRenderer()
    : tile_streamer_(new TileStreamer()),
      distant_(new DistantWorldRenderer()),
      skybox_(new Skybox()),
      profiler_(60) {
    skybox_->Init(nullptr);  // flat-colored dome (textured skybox is future work)
    // The renderer's profiler is SILENT (Inc 1 / instrumentation): rom_main
    // is the single report path and reads phase_average_ms() every 60 frames.
    // The 60-frame interval keeps the averages fresh; only the debugf
    // self-print is suppressed.
    profiler_.SetSilent(true);
    // Thread the per-frame counters + profiler into the passes (Inc 1 / D7) so
    // the room renderers can record draw counters and the texture-upload phase
    // is emitted. Both are set once in the ctor and live for the renderer's
    // lifetime.
    tile_streamer_->SetCounters(&counters_);
    tile_streamer_->SetProfiler(&profiler_);
    distant_->SetCounters(&counters_);
    // Inc 5 / D6: forward the frame-scoped arena so the distant render list
    // is allocated per-frame from it.
    distant_->SetArena(&arena_);
}

OpenWorldRenderer::~OpenWorldRenderer() {
    delete skybox_;
    delete distant_;
    delete tile_streamer_;
}

void OpenWorldRenderer::RenderDistant(const CameraDesc& cam) {
    // Inc 2 / z-split: switch to the distant projection (near=ring edge,
    // far=map diagonal) so distant cells past the near far-plane (800) actually
    // rasterize instead of clipping. The skybox draws under this projection
    // (Z-off, no near/far dependence — safe).
    if (viewport_) {
        AttachCameraAtOriginViewport(static_cast<T3DViewport*>(viewport_), cam);
    }
    // Inc 6: the skybox is drawn first (rotation-only transform), then the
    // distant cells with fog.
    skybox_->Draw(cam);
    distant_->UpdateCamera(cam.pos, cam);
    distant_->Render(cam);
}

void OpenWorldRenderer::RenderLowPriority(const CameraDesc& cam) {
    // Inc 2 stub: tile_streamer_ low-priority is a no-op. Inc 5 fills it in.
    // NOTE: this pass currently runs under the DISTANT projection (the switch
    // back to near happens in RenderHighPriority). When this pass draws water,
    // it must re-attach the near projection first — see z-split plan.
    tile_streamer_->DrawLowPriority(cam);
}

void OpenWorldRenderer::RenderHighPriority(const CameraDesc& cam) {
    // Inc 2 / z-split: restore the near projection (20..800) so the detailed
    // ring renders under the gameplay clip planes. Intra-frame only — the top
    // of the next frame re-attaches near in GameplayScene::Render.
    if (viewport_) {
        AttachCameraAtOriginViewport(static_cast<T3DViewport*>(viewport_), cam);
    }
    tile_streamer_->DrawHighPriority(cam);
}

void OpenWorldRenderer::Render(const PassCameras& cams) {
    // Inc 5 / D4: compute the near-draw set ONCE at the top (iterate the
    // resident ring through CellAabbInNearCone with the near camera), pass it
    // to the distant pass (which skips exactly those cells), then draw the
    // distant pass and the near pass. One computation, one source of truth —
    // the distant skip and the near draw can never disagree (no double-draw,
    // no mid-cell cut).
    int near_ix[9] = {};
    int near_iz[9] = {};
    int near_count = 0;
    tile_streamer_->CollectNearDrawSet(cams.near_cam, near_ix, near_iz,
                                       near_count, 9);
    distant_->SetNearDrawSet(near_ix, near_iz, near_count);

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
    // Open the per-frame profiler span. Must run at the top of Update (before
    // any SetCenter/transition) so streaming ticks are not wiped by this reset.
    profiler_.BeginFrame();
}

void OpenWorldRenderer::EndFrame() {
    // Close the per-frame profiler span (Inc 1 / instrumentation). Safe to
    // call every frame; the report fires at the 60-frame interval and is
    // silent (rom_main prints it).
    profiler_.EndFrame();
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
    // Inc 6 / D5: stream the distant tier by camera cell (resident = cells
    // within kDistantStreamRadius of `center`). Replaces the one-time Load —
    // residency re-resolves on every transition, so the old `EntryCount() == 0`
    // gate is removed (it would wrongly suppress reloads).
    distant_->StreamToCenter(spec, center, build_dir);
    // Fan the camera position to the distant pass too (compressed rebase).
    distant_->SetCameraPosition(camera_pos_);
    profiler_.EndPhase(n64::FrameProfiler::kPhaseStreaming);
}

void OpenWorldRenderer::SetCameraPosition(const Vec3& camera_pos) {
    camera_pos_ = camera_pos;
    tile_streamer_->SetCameraPosition(camera_pos);
    distant_->SetCameraPosition(camera_pos);
}

void OpenWorldRenderer::UpdateCamera() {
    // Inc 4 / D4: forward to the near pass. The near pass draws ALL residents
    // every frame (bounded ring), so this only runs the eviction safety net.
    tile_streamer_->UpdateCamera();
}

void OpenWorldRenderer::SetMaterialCatalog(const MaterialCatalog* catalog) {
    tile_streamer_->SetMaterialCatalog(catalog);
}

void OpenWorldRenderer::SetFog(const FogParams& fog) {
    distant_->SetFog(fog);
}

}  // namespace madeline_cube
