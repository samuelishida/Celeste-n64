#include "gameplay/render/distant_tier_renderer.hpp"

#include <cstdio>
#include <cstring>

#include <libdragon.h>
#include <t3d/t3d.h>

#include "gameplay/render/dlod_loader.hpp"
#include "gameplay/render/lvl_room_renderer.hpp"
#include "gameplay/world/mappack_loader.hpp"  // MapSpecV2, V2RoomSpec

namespace madeline_cube {

// Fixed-point scale the DLOD baker uses (matches LvlRoomRenderer::kLodScale).
namespace { constexpr float kLodScale = 0.25f; }

struct DistantTierRenderer::Impl {
    // ALL distant cells merged into ONE LvlRoomRenderer (SM64-style batching):
    // they share the map-center origin (v3), so one model matrix + one block
    // draws the whole horizon. The near ring is drawn on top by the near pass
    // (Z-on detail), so the merged tier's low-poly copy of the same cells is
    // occluded where it overlaps — no per-cell skip needed.
    bool loaded = false;
    LvlRoomRenderer* merged_ = nullptr;
};

DistantTierRenderer::DistantTierRenderer() : impl_(new Impl()) {}
DistantTierRenderer::~DistantTierRenderer() { FreeAll(); delete impl_; }

void DistantTierRenderer::LoadAll(const MapSpecV2& spec, const char* build_dir) {
    if (impl_->loaded) return;
    impl_->loaded = true;

    impl_->merged_ = new LvlRoomRenderer();
    const char* pack = "forsyken-city";  // the single map-pack in this build
    int appended = 0;
    for (int i = 0; i < spec.room_count && i < MapSpecV2::kMaxRooms; ++i) {
        const V2RoomSpec& rs = spec.rooms[i];
        char chunk[V2RoomSpec::kIdLen] = {};
        std::snprintf(chunk, sizeof(chunk), "%s", rs.id);
        if (AppendDistantCellDlod(pack, chunk, kLodScale, build_dir,
                                  impl_->merged_)) {
            ++appended;
        }
    }
    const bool ok = impl_->merged_->FinishAppend();
    debugf("[distant] merged %d cells into one block (finish=%s)\n",
           appended, ok ? "yes" : "no");
}

void DistantTierRenderer::SetNearCell(int cell_ix, int cell_iz) {
    // Kept for API compatibility; the merged tier draws all cells and the near
    // pass (Z detail) occludes the near-ring cells.
    (void)cell_ix; (void)cell_iz;
}

void DistantTierRenderer::FreeAll() {
    if (impl_->merged_) {
        impl_->merged_->Free();
        delete impl_->merged_;
        impl_->merged_ = nullptr;
    }
    impl_->loaded = false;
}

void DistantTierRenderer::Draw(const CameraDesc& cam) {
    // Same camera as the near pass — camera-at-origin, identical viewport.
    // Drawn AFTER the near pass (Z-on), so near-ring detail occludes the
    // merged low-poly horizon where they overlap. No per-cell matrix: the
    // merged renderer holds the shared map-center matrix.
    if (!impl_->merged_) return;
    impl_->merged_->SetCameraPosition(cam.pos);
    impl_->merged_->Draw();
}

}  // namespace madeline_cube
