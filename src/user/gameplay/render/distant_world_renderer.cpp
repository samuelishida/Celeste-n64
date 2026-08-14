#include "gameplay/render/distant_world_renderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include <rdpq.h>
#include <rdpq_mode.h>

#include "gameplay/render/lvl_room_renderer.hpp"
#include "gameplay/render/dlod_loader.hpp"
#include "gameplay/render/open_world_renderer.hpp"  // RenderCounters (Inc 1 / D7)

namespace madeline_cube {

namespace {

// Frustum-cull cone margin (Inc 2 / D2). >1 widens the cone so horizon cells
// don't pop at the exact screen edge. Tuned in Inc 6.
constexpr float kCullMargin = 1.15f;

// Direction-selection close threshold (Inc 4 / compressed-LOD): when the
// camera is within this distance of a cell center (XZ), use the camera's own
// facing instead of the cell→camera delta to avoid unstable directional
// selection (Lambert §12). ≈ 0.5 × cell size (cells are 240 world units).
constexpr float kDirectionCloseThreshold = 120.0f;

}  // namespace

// Free every loaded distant mesh. All four directional `meshes[d]` slots may
// hold DISTINCT meshes (Inc 4 / compressed-LOD), so every non-null slot is
// freed exactly once. Idempotent.
void DistantWorldRenderer::FreeEntries() {
    for (int i = 0; i < entry_count_; ++i) {
        DistantLodEntry& en = entries_[i];
        for (int d = 0; d < DistantLodEntry::kMaxDirMeshes; ++d) {
            if (en.meshes[d]) {
                en.meshes[d]->Free();
                delete en.meshes[d];
                en.meshes[d] = nullptr;
            }
        }
    }
    entry_count_ = 0;
}

DistantWorldRenderer::~DistantWorldRenderer() {
    FreeEntries();
}

bool DistantWorldRenderer::Load(const MapSpecV2& spec, const char* build_dir) {
    // Free any existing distant meshes (all directional slots).
    FreeEntries();

    if (spec.room_count > 64) return false;  // entries_ cap
    for (int i = 0; i < spec.room_count; ++i) {
        const V2RoomSpec& rs = spec.rooms[i];
        if (rs.id[0] == '\0') continue;

        // Distant geometry: prefer the compact `.dlod` (Inc 3 / compressed-LOD),
        // falling back to the `*_distant.lvl` when the `.dlod` is absent
        // (rollback-safe until Inc 5 removes the LVL2 distant path). The cell
        // id is the room id (e.g. "cell_00_00"); the pack dir is extracted
        // from the room's lvl_path (rom:/lvl/<pack>/<chunk>.lvl).
        char pack_dir[V2RoomSpec::kPathLen] = {};
        {
            const char* p = rs.lvl_path;
            // Skip "rom:/lvl/".
            const char* start = std::strstr(p, "lvl/");
            if (start) start += 4;
            else start = p;
            const char* slash = std::strchr(start, '/');
            size_t len = slash ? static_cast<size_t>(slash - start)
                               : std::strlen(start);
            if (len >= sizeof(pack_dir)) len = sizeof(pack_dir) - 1;
            std::memcpy(pack_dir, start, len);
            pack_dir[len] = '\0';
        }

        // Load the .dlod (all 4 directions) or fall back to the .lvl.
        LvlRoomRenderer* meshes[DistantLodEntry::kMaxDirMeshes] = {};
        int loaded = LoadDistantCellDlodAll(pack_dir, rs.id, rs.render_origin,
                                            kLodScale, build_dir, meshes);
        if (loaded <= 0) {
            // A cell with no distant geometry (no renderable geometry) is
            // skipped.
            continue;
        }

        DistantLodEntry& en = entries_[entry_count_];
        en = DistantLodEntry{};
        en.lod_scale = kLodScale;
        en.priority = 1;
        en.cell_ix = rs.cell_ix;
        en.cell_iz = rs.cell_iz;
        en.origin = rs.render_origin;
        en.child_count = 0;
        for (int d = 0; d < DistantLodEntry::kMaxDirMeshes; ++d) {
            en.meshes[d] = meshes[d];
        }
        ++entry_count_;
    }
    return entry_count_ > 0;
}

void DistantWorldRenderer::SetCameraPosition(const Vec3& camera_pos) {
    camera_pos_ = camera_pos;
    for (int i = 0; i < entry_count_; ++i) {
        for (int d = 0; d < DistantLodEntry::kMaxDirMeshes; ++d) {
            if (entries_[i].meshes[d]) {
                entries_[i].meshes[d]->SetCameraPosition(camera_pos);
            }
        }
    }
}

void DistantWorldRenderer::UpdateCamera(const Vec3& camera_pos,
                                        const CameraDesc&) {
    camera_pos_ = camera_pos;
}

void DistantWorldRenderer::SetFog(const FogParams& fog) {
    fog_ = fog;
}

void DistantWorldRenderer::Render(const CameraDesc& cam) {
    // Inc 2 / D2: horizontal full FOV from the vertical FOV + 4:3 aspect
    // (hfov = 2·atan(tan(vfov/2)·(4/3))). The cull depth range is the DISTANT
    // pass clip range (cam.near/cam.far) in WORLD-SPACE units — near = just
    // past the resident ring, far = full map diagonal (see MakeDistantCamera).
    // Cells behind the near far-plane must still be drawn here.
    const float vfov_rad = cam.fov_deg * 0.5f * (kLodPi / 180.0f);
    const float hfov_deg = 2.0f * std::atan(std::tan(vfov_rad) * (4.0f / 3.0f)) *
                           (180.0f / kLodPi);

    // Build the render list, culled + distance-ordered (arch.md §8, Inc 2/D2).
    // Inc 5 / D6: allocate the list from the frame-scoped arena (reset each
    // frame); fall back to a small stack buffer if the arena is absent or
    // exhausted (64 KB is ample for ≤ 64 items — the fallback is a safety net).
    DistantRenderItem stack_order[64];
    DistantRenderItem* order = stack_order;
    if (arena_) {
        DistantRenderItem* buf = static_cast<DistantRenderItem*>(
            arena_->Alloc(sizeof(DistantRenderItem) * 64));
        if (buf) order = buf;
    }
    const int n = BuildDistantRenderListCulled(
        camera_pos_, cam.target, entries_, entry_count_, order, 64,
        hfov_deg, cam.near, cam.far, kCullMargin, kDistantMaxDist2);

    // Reset the per-cell cost capture array (Inc 3 / instrumentation).
    cell_stat_count_ = 0;

    // arch.md §7-8: Z off, draw farthest first, then restore Z.
    rdpq_sync_pipe();
    rdpq_mode_zbuf(false, false);

    // Fog (Inc 6): configure the RDP fog mode + color/range for the distant
    // pass so the horizon fades into the atmosphere. Torn down after.
    if (fog_.enabled && ValidateFogRange(fog_)) {
        rdpq_mode_fog(RDPQ_FOG_STANDARD);
        rdpq_set_fog_color((color_t){
            (uint8_t)fog_.color.x, (uint8_t)fog_.color.y,
            (uint8_t)fog_.color.z, 0xFF});
        t3d_fog_set_range(fog_.min, fog_.max);
        t3d_fog_set_enabled(true);
    }

    // Sort back-to-front: BuildDistantRenderListCulled sets priority =
    // distance², so descending priority draws far cells first.
    std::sort(order, order + n,
              [](const DistantRenderItem& a, const DistantRenderItem& b) {
                  return a.priority > b.priority;
              });

    for (int i = 0; i < n; ++i) {
        const int e = order[i].cell_index;
        if (e < 0 || e >= entry_count_) continue;
        DistantLodEntry& en = entries_[e];
        // Inc 4 / compressed-LOD: select the directional mesh facing the
        // camera. Near the cell center, use the camera's own facing to avoid
        // unstable selection (Lambert §12). Fall back to meshes[0] if the
        // selected slot is null, else skip.
        const Vec3 cam_dir = {cam.target.x - camera_pos_.x,
                              cam.target.y - camera_pos_.y,
                              cam.target.z - camera_pos_.z};
        const int d = DirectionalMeshIndex(camera_pos_, en.origin, cam_dir,
                                           kDirectionCloseThreshold);
        LvlRoomRenderer* mesh = en.meshes[d];
        if (!mesh) mesh = en.meshes[0];
        if (!mesh) continue;
        // Inc 1 / D7: count cells drawn (once per cell).
        if (counters_) ++counters_->distant_cells;
        mesh->Draw();

        // Inc 2 / instrumentation: attribute this cell's draw cost. The active
        // path (runs vs per-face batches) mirrors Draw()'s own gate exactly, so
        // the sync count is the true RSP sync count. Guard counters_ null.
        const int units = mesh->IsActiveRunPath() ? mesh->RunCount()
                                                  : mesh->BatchCount();
        if (units > 0 && counters_) {
            counters_->distant_batches += static_cast<uint32_t>(units);
            counters_->distant_vert_loads += static_cast<uint32_t>(units);
            counters_->distant_syncs += static_cast<uint32_t>(units);
        }

        // Inc 3 / instrumentation: capture a per-cell cost summary (bounded to
        // the entries cap). `order[i].distance` is dx²+dz² (distance²).
        if (cell_stat_count_ < kDistantCellStatCap) {
            DistantCellStat& st = cell_stats_[cell_stat_count_++];
            st.cell_ix = en.cell_ix;
            st.cell_iz = en.cell_iz;
            st.runs = units > 0 ? units : 0;
            st.verts = mesh->VertexCount();
            st.distance_sq = order[i].distance;
        }
    }

    // Tear down fog + Z before the near pass.
    if (fog_.enabled) {
        t3d_fog_set_enabled(false);
        rdpq_mode_fog(0);
    }
    rdpq_sync_pipe();
    rdpq_mode_zbuf(true, true);
}

}  // namespace madeline_cube
