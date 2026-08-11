#include "gameplay/render/open_world_renderer.hpp"

#include "gameplay/render/chunk_ring_renderer.hpp"
#include "gameplay/render/distant_world_renderer.hpp"
#include "gameplay/render/tile_streamer.hpp"
#include "gameplay/world/mappack_loader.hpp"

namespace madeline_cube {

OpenWorldRenderer::OpenWorldRenderer()
    : ring_(new ChunkRingRenderer()),
      tile_streamer_(new TileStreamer()),
      distant_(new DistantWorldRenderer()) {}

OpenWorldRenderer::~OpenWorldRenderer() {
    delete distant_;
    delete tile_streamer_;
    delete ring_;
}

void OpenWorldRenderer::RenderDistant(const CameraDesc& cam) {
    // Inc 2: stub distant renderer flips Z off/on. Inc 4 draws LOD cells.
    // Inc 6 adds the skybox + fog before the distant cells.
    distant_->UpdateCamera(cam.pos, cam);
    distant_->Render(cam);
}

void OpenWorldRenderer::RenderLowPriority(const CameraDesc& cam) {
    // Inc 2 stub: tile_streamer_ low-priority is a no-op. Inc 5 fills it in.
    tile_streamer_->DrawLowPriority(cam);
}

void OpenWorldRenderer::RenderHighPriority(const CameraDesc& cam) {
    // Inc 2: draw the legacy ring (no regression). Inc 3 swaps to
    // tile_streamer_->DrawHighPriority(cam).
    if (ring_) ring_->Draw();
}

void OpenWorldRenderer::Render(const PassCameras& cams) {
    // arch.md §21 order: distant (Z off), low-priority (Z off), high-priority
    // (Z on). The skybox is prepended in Inc 6.
    RenderDistant(cams.distant_cam);
    RenderLowPriority(cams.near_cam);
    RenderHighPriority(cams.near_cam);
}

void OpenWorldRenderer::SetCenter(const MapSpecV2& spec,
                                  const V2RoomSpec& center,
                                  const char* build_dir) {
    if (ring_) ring_->Load(spec, center, build_dir);
}

void OpenWorldRenderer::SetCameraPosition(const Vec3& camera_pos) {
    if (ring_) ring_->SetCameraPosition(camera_pos);
}

}  // namespace madeline_cube
