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
    // (Z on). The skybox is prepended in Inc 6.
    RenderDistant(cams.distant_cam);
    RenderLowPriority(cams.near_cam);
    RenderHighPriority(cams.near_cam);
}

void OpenWorldRenderer::SetCenter(const MapSpecV2& spec,
                                  const V2RoomSpec& center,
                                  const char* build_dir) {
    tile_streamer_->SetCenter(spec, center, build_dir);
    // Load the distant LOD table once per map-pack (all cells, coarse).
    // Inc 4: the distant pass renders the horizon from these coarse meshes.
    if (distant_->EntryCount() == 0) {
        distant_->Load(spec, build_dir);
    }
    // Fan the camera position to the distant pass too (compressed rebase).
    distant_->SetCameraPosition(camera_pos_);
}

void OpenWorldRenderer::SetCameraPosition(const Vec3& camera_pos) {
    camera_pos_ = camera_pos;
    tile_streamer_->SetCameraPosition(camera_pos);
    distant_->SetCameraPosition(camera_pos);
}

void OpenWorldRenderer::SetMaterialCatalog(const MaterialCatalog* catalog) {
    tile_streamer_->SetMaterialCatalog(catalog);
}

void OpenWorldRenderer::SetFog(const FogParams& fog) {
    distant_->SetFog(fog);
}

}  // namespace madeline_cube
