#include "gameplay/render/distant_tier_renderer.hpp"

#include <cstdio>

#include <libdragon.h>
#include <rdpq.h>
#include <rdpq_mode.h>
#include <t3d/t3d.h>

#include "gameplay/render/dlod_loader.hpp"
#include "gameplay/render/lod_math.hpp"
#include "gameplay/render/lvl_room_renderer.hpp"
#include "gameplay/render/open_world_renderer.hpp"  // RenderCounters
#include "gameplay/render/tile_streamer.hpp"
#include "gameplay/world/mappack_loader.hpp"       // MapSpecV2, V2RoomSpec

namespace madeline_cube {

namespace {

constexpr float kLodScale = 0.25f;
constexpr char kPackName[] = "forsyken-city";

bool SameOrigin(const Vec3& a, const Vec3& b) {
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

}  // namespace

struct DistantTierRenderer::Impl {
    struct Cell {
        LvlRoomRenderer* renderer = nullptr;
        AABB bounds = {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}};
        int cell_ix = 0;
        int cell_iz = 0;
    };

    bool loaded = false;
    int cell_count = 0;
    Cell cells[MapSpecV2::kMaxRooms];
    FogParams fog;  // disabled by default; SetFog configures the horizon fade
};

DistantTierRenderer::DistantTierRenderer() : impl_(new Impl()) {}
DistantTierRenderer::~DistantTierRenderer() { FreeAll(); delete impl_; }

void DistantTierRenderer::LoadAll(const MapSpecV2& spec, const char* build_dir) {
    if (impl_->loaded) return;
    impl_->loaded = true;

    uint32_t face_count = 0;
    for (int i = 0; i < spec.room_count && i < MapSpecV2::kMaxRooms; ++i) {
        const V2RoomSpec& room = spec.rooms[i];
        if (room.id[0] == '\0') continue;

        LvlRoomRenderer* renderer = new LvlRoomRenderer();
        if (!LoadDistantCellDlod(kPackName, room.id, room.render_origin,
                                 kLodScale, build_dir, renderer)) {
            delete renderer;
            continue;
        }

        Impl::Cell& cell = impl_->cells[impl_->cell_count++];
        cell.renderer = renderer;
        cell.bounds = room.world_aabb;
        cell.cell_ix = room.cell_ix;
        cell.cell_iz = room.cell_iz;
        face_count += static_cast<uint32_t>(renderer->VertexCount() / 3);
    }

    debugf("[distant] loaded cells=%d faces=%u (one direction per cell)\n",
           impl_->cell_count, static_cast<unsigned int>(face_count));
}

void DistantTierRenderer::FreeAll() {
    if (!impl_) return;
    for (int i = 0; i < impl_->cell_count; ++i) {
        delete impl_->cells[i].renderer;
        impl_->cells[i] = Impl::Cell{};
    }
    impl_->cell_count = 0;
    impl_->loaded = false;
}

void DistantTierRenderer::SetFog(const FogParams& fog) {
    impl_->fog = fog;
}

void DistantTierRenderer::Draw(const CameraDesc& cam,
                               RenderCounters* counters,
                               const TileStreamer& near_streamer) {
    if (!impl_->loaded || impl_->cell_count == 0 ||
        !ValidateDistantCamera(cam)) {
        return;
    }

    int visible[MapSpecV2::kMaxRooms];
    int visible_count = 0;
    for (int i = 0; i < impl_->cell_count; ++i) {
        const Impl::Cell& cell = impl_->cells[i];

        if (near_streamer.IsCellDrawn(cell.cell_ix, cell.cell_iz)) continue;
        if (!CellAabbInFrustum3D(cam.pos, cam.target, cam.up, cam.fov_deg,
                                 4.0f / 3.0f, cam.near, cam.far,
                                 cell.bounds, kCullMargin)) {
            continue;
        }
        visible[visible_count++] = i;
    }
    if (visible_count == 0) return;

    // Every DLOD artifact is baked against the same map-center origin. Push
    // one camera-relative transform, then draw only visible cell blocks.
    // If an artifact violates the shared-origin contract, fall back to each
    // renderer's own matrix instead of misplacing geometry.
    LvlRoomRenderer* first = impl_->cells[visible[0]].renderer;
    const Vec3 shared_origin = first->RenderOrigin();
    bool shared = true;
    for (int i = 1; i < visible_count; ++i) {
        if (!SameOrigin(shared_origin,
                        impl_->cells[visible[i]].renderer->RenderOrigin())) {
            shared = false;
            break;
        }
    }

    const auto count_cell = [counters](const Impl::Cell& cell) {
        if (!counters) return;
        const LvlRoomRenderer& renderer = *cell.renderer;
        ++counters->distant_cells;
        counters->distant_faces += static_cast<uint32_t>(
            renderer.VertexCount() / 3);
        counters->distant_batches += renderer.DrawBatchCount();
        counters->distant_vert_loads += renderer.DrawVertLoadCount();
        counters->distant_syncs += renderer.DrawSyncCount();
    };

    // Fog (Inc 6): fade the horizon into atmosphere. Applied in the distant
    // projection's depth space and torn down before the near pass so the
    // detailed ring draws crisp.
    const bool fog_on = impl_->fog.enabled && ValidateFogRange(impl_->fog);
    if (fog_on) {
        rdpq_mode_fog(RDPQ_FOG_STANDARD);
        rdpq_set_fog_color((color_t){
            (uint8_t)impl_->fog.color.x, (uint8_t)impl_->fog.color.y,
            (uint8_t)impl_->fog.color.z, 0xFF});
        t3d_fog_set_range(impl_->fog.min, impl_->fog.max);
        t3d_fog_set_enabled(true);
    }

    if (shared) {
        first->SetCameraPosition(cam.pos);
        t3d_matrix_push(first->CurrentMatrixFP());
        for (int i = 0; i < visible_count; ++i) {
            Impl::Cell& cell = impl_->cells[visible[i]];
            cell.renderer->DrawNoMatrix();
            count_cell(cell);
        }
        t3d_matrix_pop(1);
    } else {
        for (int i = 0; i < visible_count; ++i) {
            Impl::Cell& cell = impl_->cells[visible[i]];
            cell.renderer->SetCameraPosition(cam.pos);
            cell.renderer->Draw();
            count_cell(cell);
        }
    }

    // Tear down fog before the near pass so detailed geometry draws crisp.
    if (fog_on) {
        rdpq_sync_pipe();
        t3d_fog_set_enabled(false);
        rdpq_mode_fog(0);
    }
}

}  // namespace madeline_cube
