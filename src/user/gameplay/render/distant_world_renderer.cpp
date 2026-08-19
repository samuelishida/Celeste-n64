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

// Direction-selection close threshold (Inc 4 / compressed-LOD): when the
// camera is within this distance of a cell center (XZ), use the camera's own
// facing instead of the cell→camera delta to avoid unstable directional
// selection (Lambert §12). ≈ 0.5 × cell size (cells are 240 world units).
constexpr float kDirectionCloseThreshold = 120.0f;

// Free one entry's distinct directional meshes (Inc 2 / double-free fix):
// dedupe the shared-slot pointers, Free() + delete each distinct mesh once,
// then null all slots. Idempotent. Shared by `FreeEntries` and the
// `StreamToCenter` eviction path so the cleanup can't drift.
void FreeEntryMeshes(DistantLodEntry& en) {
    LvlRoomRenderer* distinct[DistantLodEntry::kMaxDirMeshes] = {};
    const int n = CollectDistinctMeshes(en, distinct);
    for (int k = 0; k < n; ++k) {
        distinct[k]->Free();
        delete distinct[k];
    }
    for (int d = 0; d < DistantLodEntry::kMaxDirMeshes; ++d) {
        en.meshes[d] = nullptr;
    }
}

}  // namespace

// Free every loaded distant mesh. All four directional `meshes[d]` slots may
// hold DISTINCT meshes (Inc 4 / compressed-LOD), but a single-direction
// `.dlod` path can put the SAME pointer in all 4 slots — so we dedupe
// distinct pointers per entry and free each exactly once (Inc 2 / double-free
// fix). Idempotent.
void DistantWorldRenderer::FreeEntries() {
    for (int i = 0; i < entry_count_; ++i) {
        FreeEntryMeshes(entries_[i]);
    }
    entry_count_ = 0;
}

DistantWorldRenderer::~DistantWorldRenderer() {
    FreeEntries();
    if (shared_matrix_fp_) {
        free_uncached(shared_matrix_fp_);
        shared_matrix_fp_ = nullptr;
    }
}
bool DistantWorldRenderer::Load(const MapSpecV2& spec, const char* build_dir) {
    // Free any existing distant meshes (all directional slots).
    FreeEntries();

    if (spec.room_count > 64) return false;  // entries_ cap
    for (int i = 0; i < spec.room_count; ++i) {
        const V2RoomSpec& rs = spec.rooms[i];
        if (rs.id[0] == '\0') continue;
        LoadCell(rs, build_dir);
    }
    return entry_count_ > 0;
}

bool DistantWorldRenderer::LoadCell(const V2RoomSpec& rs,
                                    const char* build_dir) {
    // Distant geometry: the compact `.dlod` (Inc 3 / compressed-LOD). The cell
    // id is the room id (e.g. "cell_00_00"); the pack dir is extracted from
    // the room's lvl_path (rom:/lvl/<pack>/<chunk>.lvl).
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

    // Load the .dlod (all 4 directions).
    LvlRoomRenderer* meshes[DistantLodEntry::kMaxDirMeshes] = {};
    Vec3 shared_origin = {0.0f, 0.0f, 0.0f};
    int loaded = LoadDistantCellDlodAll(pack_dir, rs.id, kLodScale, build_dir,
                                        meshes, &shared_origin);
    if (loaded <= 0) {
        // A cell with no distant geometry (no renderable geometry) is skipped.
        return false;
    }
    // Inc 3 / D2: the DLOD header origin is the SHARED map-center origin (the
    // source of truth for packing). Store it once (all cells share it) so the
    // pass can build ONE camera-relative matrix.
    shared_origin_ = shared_origin;

    DistantLodEntry& en = entries_[entry_count_];
    en = DistantLodEntry{};
    en.lod_scale = kLodScale;
    en.priority = 1;
    en.cell_ix = rs.cell_ix;
    en.cell_iz = rs.cell_iz;
    en.origin = rs.render_origin;
    // Inc 4 / D3: the cell's world XZ AABB (from the manifest) for the
    // extent-aware distant cull.
    en.aabb = rs.world_aabb;
    en.child_count = 0;
    for (int d = 0; d < DistantLodEntry::kMaxDirMeshes; ++d) {
        en.meshes[d] = meshes[d];
        // Inc 3 / D2: distant meshes draw under the pass-shared matrix — mark
        // them so SetCameraPosition is a no-op and DrawBlockOnly is the draw
        // path. (No-block mode is set in dlod_loader.cpp BEFORE LoadFromDlod,
        // because the RSPQ block capture happens inside LoadFromDlod.)
        if (meshes[d]) meshes[d]->SetExternalMatrixOwner();
    }
    ++entry_count_;
    return true;
}

bool DistantWorldRenderer::StreamToCenter(const MapSpecV2& spec,
                                          const V2RoomSpec& center,
                                          const char* build_dir) {
    // Inc 6 / D5: stream the distant tier by camera cell. Compute the target
    // resident set (cells within kDistantStreamRadius Chebyshev of `center`),
    // evict entries outside it (via the Inc 2 dedupe), load missing in-radius
    // cells. Runs in SetCenter (Update phase, before the frame's Render builds
    // its draw list) — no eviction between list-build and draw (use-after-free
    // guard).
    if (spec.room_count > 64) return false;  // entries_ cap + rooms[] bounds

    // 1. Evict entries outside the radius. Walk backwards so removals don't
    //    shift unvisited indices.
    for (int i = entry_count_ - 1; i >= 0; --i) {
        DistantLodEntry& en = entries_[i];
        const int cheb = ChebyshevCellDistance(en.cell_ix, en.cell_iz,
                                               center.cell_ix, center.cell_iz);
        if (cheb > kDistantStreamRadius) {
            FreeEntryMeshes(en);
            // Shift the tail down.
            for (int j = i; j < entry_count_ - 1; ++j) {
                entries_[j] = entries_[j + 1];
            }
            --entry_count_;
        }
    }

    // 2. Load missing in-radius cells.
    for (int i = 0; i < spec.room_count; ++i) {
        const V2RoomSpec& rs = spec.rooms[i];
        if (rs.id[0] == '\0') continue;
        const int cheb = ChebyshevCellDistance(rs.cell_ix, rs.cell_iz,
                                               center.cell_ix, center.cell_iz);
        if (cheb > kDistantStreamRadius) continue;
        // Already resident?
        bool resident = false;
        for (int e = 0; e < entry_count_; ++e) {
            if (entries_[e].cell_ix == rs.cell_ix &&
                entries_[e].cell_iz == rs.cell_iz) {
                resident = true;
                break;
            }
        }
        if (resident) continue;
        if (entry_count_ >= 64) break;  // entries_ cap
        LoadCell(rs, build_dir);
    }
    return entry_count_ > 0;
}

void DistantWorldRenderer::SetCameraPosition(const Vec3& camera_pos) {
    // Inc 3 / D2: store the camera position only. The shared pass matrix is
    // rebuilt once per frame in Render() (from shared_origin_ - camera_pos_),
    // so there is no per-mesh matrix rebuild storm.
    camera_pos_ = camera_pos;
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

    // Inc 3 / D2: build ONE shared camera-relative matrix for the whole
    // distant pass (all verts pack against shared_origin_ at kLodScale) and
    // push it once. Every cell's block draws under it via DrawBlockOnly (no
    // per-cell push/pop, no per-mesh matrix rebuild). No cells loaded → no-op.
    // The matrix lives in UNCACHED memory (the RSP DMAs it at command-execution
    // time, so a cached stack local would be read stale).
    if (n > 0) {
        if (!shared_matrix_fp_) {
            shared_matrix_fp_ = malloc_uncached(sizeof(T3DMat4FP));
        }
        if (shared_matrix_fp_) {
            T3DMat4FP* mat_fp = static_cast<T3DMat4FP*>(shared_matrix_fp_);
            const float s[3] = {1.0f / kLodScale, 1.0f / kLodScale,
                                1.0f / kLodScale};
            const float r[3] = {0, 0, 0};
            const float p[3] = {shared_origin_.x - camera_pos_.x,
                                shared_origin_.y - camera_pos_.y,
                                shared_origin_.z - camera_pos_.z};
            t3d_mat4fp_from_srt_euler(mat_fp, s, r, p);
            t3d_matrix_push(mat_fp);
        }
    }

    for (int i = 0; i < n; ++i) {
        const int e = order[i].cell_index;
        if (e < 0 || e >= entry_count_) continue;
        DistantLodEntry& en = entries_[e];
        // Inc 5 / D4: skip the exact cells the near pass will draw (overlap
        // handoff) so no cell is drawn by both passes. The near-draw set is
        // computed once per frame by the orchestrator.
        bool in_near_set = false;
        for (int k = 0; k < near_count_; ++k) {
            if (en.cell_ix == near_ix_[k] && en.cell_iz == near_iz_[k]) {
                in_near_set = true;
                break;
            }
        }
        if (in_near_set) continue;
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
        // streaming-memory-opt Inc 3: emit the cell's runs DIRECTLY under the
        // shared pass matrix (no per-cell push, NO RSPQ block). The (run, face)
        // sequence is identical to the old DrawBlockOnly path, so silhouettes
        // are unchanged; the cell allocated zero blocks at load.
        mesh->DrawRunsDirect();

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

    // Pop the shared pass matrix (only if we pushed it).
    if (n > 0 && shared_matrix_fp_) {
        t3d_matrix_pop(1);
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
