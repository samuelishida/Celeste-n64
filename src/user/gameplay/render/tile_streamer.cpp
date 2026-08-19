#include "gameplay/render/tile_streamer.hpp"

#include <cstdio>
#include <cstring>

#include "gameplay/render/lvl_room_renderer.hpp"
#include "gameplay/render/open_world_renderer.hpp"  // RenderCounters (Inc 1 / D7)
#include "gameplay/render/textured_room_renderer.hpp"
#include "n64/profiler.hpp"  // FrameProfiler (Inc 1 / D7)

namespace madeline_cube {

namespace {

// Localize a "rom:/lvl/<pack>/<chunk>.lvl" path to a filesystem path
// "<build_dir>/<chunk>.lvl" for host-side loading. On device (build_dir null)
// the rom:/ path is used as-is.
const char* LocalizePath(const char* rom_path, const char* build_dir) {
    if (!build_dir || build_dir[0] == '\0') return rom_path;
    static thread_local char local[256];
    const char* slash = std::strrchr(rom_path, '/');
    const char* fname = slash ? slash + 1 : rom_path;
    std::snprintf(local, sizeof(local), "%s/%s", build_dir, fname);
    return local;
}

}  // namespace

TileStreamer::~TileStreamer() {
    for (int i = 0; i < set_.count; ++i) {
        if (renderers_[i]) {
            renderers_[i]->Free();
            delete renderers_[i];
            renderers_[i] = nullptr;
        }
        if (textured_renderers_[i]) {
            textured_renderers_[i]->Free();
            delete textured_renderers_[i];
            textured_renderers_[i] = nullptr;
        }
    }
    set_.count = 0;
}

void TileStreamer::SetMaterialCatalog(const MaterialCatalog* catalog) {
    catalog_ = catalog;
}

void TileStreamer::SetCounters(RenderCounters* counters) {
    counters_ = counters;
    // Forward to already-resident renderers (idempotent; the orchestrator
    // calls this once in its ctor before any SetCenter).
    for (int i = 0; i < set_.count; ++i) {
        if (renderers_[i]) renderers_[i]->SetCounters(counters_);
        if (textured_renderers_[i]) textured_renderers_[i]->SetCounters(counters_);
    }
}

void TileStreamer::SetProfiler(n64::FrameProfiler* profiler) {
    profiler_ = profiler;
}

bool TileStreamer::SetCenter(const MapSpecV2& spec, const V2RoomSpec& center,
                             const char* build_dir) {
    // Inc 4 / D4: the near pass draws ALL residents every frame, so there is
    // no per-frame visibility mask to reset here.

    // Resolve the distance ring (center + Chebyshev-1 neighbors).
    const V2RoomSpec* ring[kMaxRing] = {};
    const int count = ResolveDistanceRing(spec, center, ring, kMaxRing);
    if (count == 0) return false;

    // streaming-memory-opt Inc 1: diff the new ring against the current
    // residents instead of free-all + load-all. A center→neighbor transition
    // keeps the overlapping cells (no reload, no RSPQ recompile) and only
    // loads the new cells / frees the ones that left. This kills the primary
    // streaming hitch (Decision 1).
    const RingDiffResult diff = ResolveRingDiff(set_, ring, count);

    // Free every resident (both flat and textured) and reset the pool. Used on
    // a center load failure to match the old "reset the ring" behavior without
    // leaking the kept residents.
    auto free_all = [this]() {
        for (int k = 0; k < set_.count; ++k) {
            if (renderers_[k]) {
                renderers_[k]->Free();
                delete renderers_[k];
                renderers_[k] = nullptr;
            }
            if (textured_renderers_[k]) {
                textured_renderers_[k]->Free();
                delete textured_renderers_[k];
                textured_renderers_[k] = nullptr;
            }
        }
        set_.count = 0;
    };

    // Free the residents that left the ring (exactly once each).
    for (int i = 0; i < diff.free_count; ++i) {
        const int k = set_.IndexOf(diff.free[i]);
        if (k < 0) continue;  // defensive: already gone
        if (renderers_[k]) {
            renderers_[k]->Free();
            delete renderers_[k];
            renderers_[k] = nullptr;
        }
        if (textured_renderers_[k]) {
            textured_renderers_[k]->Free();
            delete textured_renderers_[k];
            textured_renderers_[k] = nullptr;
        }
    }

    // Load the new cells (the center is ring[0], so it loads first). Stash each
    // new renderer in a side table keyed by load index so the compaction below
    // can place it in the correct ring slot. A center load failure is fatal
    // (reset + false); a non-center failure is skipped (existing behavior).
    LvlRoomRenderer* loaded_flat[kMaxRing] = {};
    TexturedRoomRenderer* loaded_tex[kMaxRing] = {};
    for (int i = 0; i < diff.load_count; ++i) {
        const V2RoomSpec& rs = *diff.load[i];
        const bool is_center = (&rs == &center);
        const char* path = LocalizePath(rs.lvl_path, build_dir);
        if (kEnableTextures && catalog_) {
            TexturedRoomRenderer* tr = new TexturedRoomRenderer();
            if (!tr->Load(path, rs.render_origin, catalog_)) {
                delete tr;
                if (is_center) { free_all(); return false; }
                continue;
            }
            tr->SetCounters(counters_);  // Inc 1 / D7
            loaded_tex[i] = tr;
        } else {
            LvlRoomRenderer* r = new LvlRoomRenderer();
            if (!r->Load(path, rs.render_origin)) {
                delete r;
                if (is_center) { free_all(); return false; }
                continue;
            }
            r->SetCounters(counters_);  // Inc 1 / D7
            loaded_flat[i] = r;
        }
    }

    // Compact the parallel arrays to the new ring order (center first). For
    // each ring cell, reuse the kept renderer (and its last_used) or the newly
    // loaded one (last_used = 0). Read all old data into a temp buffer first so
    // the in-place rewrite never clobbers a slot we still need.
    LvlRoomRenderer* old_flat[kMaxRing] = {};
    TexturedRoomRenderer* old_tex[kMaxRing] = {};
    uint32_t old_last_used[kMaxRing] = {};
    for (int k = 0; k < set_.count; ++k) {
        old_flat[k] = renderers_[k];
        old_tex[k] = textured_renderers_[k];
        old_last_used[k] = set_.last_used[k];
    }

    int n = 0;
    for (int i = 0; i < count; ++i) {
        const V2RoomSpec* s = ring[i];
        LvlRoomRenderer* flat = nullptr;
        TexturedRoomRenderer* tex = nullptr;
        uint32_t last_used = 0;
        const int old_idx = set_.IndexOf(s);
        if (old_idx >= 0) {
            // Kept cell: reuse the existing renderer + last_used.
            flat = old_flat[old_idx];
            tex = old_tex[old_idx];
            last_used = old_last_used[old_idx];
        } else {
            // Loaded cell: use the newly loaded renderer (last_used = 0).
            for (int li = 0; li < diff.load_count; ++li) {
                if (diff.load[li] == s) {
                    flat = loaded_flat[li];
                    tex = loaded_tex[li];
                    break;
                }
            }
            // A non-center load that failed is skipped: flat/tex stay null and
            // the cell is simply not added (existing behavior).
            if (!flat && !tex) continue;
        }
        renderers_[n] = flat;
        textured_renderers_[n] = tex;
        set_.spec[n] = s;
        set_.last_used[n] = last_used;
        ++n;
    }
    // Clear any trailing slots beyond the new count (stale moved-from / freed
    // pointers) so the destructor never double-frees.
    for (int k = n; k < kMaxRing; ++k) {
        renderers_[k] = nullptr;
        textured_renderers_[k] = nullptr;
        set_.spec[k] = nullptr;
        set_.last_used[k] = 0;
    }
    set_.count = n;
    return n > 0;
}

void TileStreamer::UpdateCamera() {
    // The ring is kept fresh by `SetCenter`, which the orchestrator calls on
    // every cross-cell transition (GameplayScene::TransitionToRoom / BootMapPack).
    // The near pass draws ALL residents every frame (the pool is bounded to the
    // center + Chebyshev-1 ring, ≤9 cells), so there is no per-frame frustum
    // visibility resolution: a brush is assigned to a cell by its center and
    // can overflow into a neighbor cell, so cell-level culling would cut
    // geometry at the cell boundary when the camera rotates. This hook only
    // runs the over-capacity eviction safety net (currently unreachable — the
    // ring never exceeds kMaxRing — but preserved for streaming robustness).
    evicted_this_frame_ = 0;
    if (set_.OverCapacity()) {
        while (set_.count > set_.capacity) {
            const int victim = set_.EvictCandidate(set_.spec[0]);
            if (victim < 0) break;  // nothing evictable (all center)
            if (renderers_[victim]) {
                renderers_[victim]->Free();
                delete renderers_[victim];
                renderers_[victim] = nullptr;
            }
            if (textured_renderers_[victim]) {
                textured_renderers_[victim]->Free();
                delete textured_renderers_[victim];
                textured_renderers_[victim] = nullptr;
            }
            for (int k = victim; k < set_.count - 1; ++k) {
                set_.spec[k] = set_.spec[k + 1];
                set_.last_used[k] = set_.last_used[k + 1];
                renderers_[k] = renderers_[k + 1];
                textured_renderers_[k] = textured_renderers_[k + 1];
            }
            --set_.count;
            ++evicted_this_frame_;
        }
    }
}

void TileStreamer::SetCameraPosition(const Vec3& camera_pos) {
    for (int i = 0; i < set_.count; ++i) {
        if (renderers_[i]) renderers_[i]->SetCameraPosition(camera_pos);
        if (textured_renderers_[i]) textured_renderers_[i]->SetCameraPosition(camera_pos);
    }
}

void TileStreamer::DrawLowPriority(const CameraDesc&) {
    // Inc 3: the near subpass is a no-op here (no water/background yet).
    // Inc 5 fills it in.
}

void TileStreamer::CollectNearDrawSet(const CameraDesc& cam, int out_ix[],
                                      int out_iz[], int& out_count,
                                      int out_capacity) const {
    out_count = 0;
    if (!out_ix || !out_iz || out_capacity <= 0) return;
    for (int i = 0; i < set_.count && out_count < out_capacity; ++i) {
        const V2RoomSpec& rs = *set_.spec[i];
        if (!CellAabbInNearCone(cam.pos, cam.target, cam.fov_deg, cam.near,
                                cam.far, rs.world_aabb)) {
            continue;
        }
        out_ix[out_count] = rs.cell_ix;
        out_iz[out_count] = rs.cell_iz;
        ++out_count;
    }
}

void TileStreamer::DrawHighPriority(const CameraDesc& cam) {
    // Inc 5 / D4: draw exactly the resident cells whose AABB intersects the
    // camera cone (AABB-cone test, no grid-index cut). The old grid-index gate
    // was removed because it cut geometry at cell boundaries during rotation;
    // an AABB-cone test cannot cut mid-cell. The distant pass skips exactly
    // this same set (computed once per frame by the orchestrator), so the two
    // passes are disjoint — no double-draw band, no mid-cell cut.

    // streaming-memory-opt Inc 4: the near pass is fully opaque (the depth
    // buffer resolves order), so draws can be reordered by material without
    // changing the visible result. When kEnableGlobalMaterialGrouping is on,
    // we group the near pass per-material → per-cell so each TMEM sprite is
    // uploaded ONCE per material instead of once per (material, cell). The
    // flat (untextured) fallback renderers are drawn per-cell as before.

    // Pass 1: flat (untextured) fallback renderers, per-cell as before.
    for (int i = 0; i < set_.count; ++i) {
        const V2RoomSpec& rs = *set_.spec[i];
        if (!CellAabbInNearCone(cam.pos, cam.target, cam.fov_deg, cam.near,
                                cam.far, rs.world_aabb)) {
            continue;  // resident but off-cone — not drawn this frame
        }
        if (renderers_[i]) {
            renderers_[i]->Draw();
        }
    }

    // Pass 2: textured renderers.
    if (!kEnableGlobalMaterialGrouping) {
        // Legacy per-cell block path (A/B fallback).
        for (int i = 0; i < set_.count; ++i) {
            const V2RoomSpec& rs = *set_.spec[i];
            if (!CellAabbInNearCone(cam.pos, cam.target, cam.fov_deg, cam.near,
                                    cam.far, rs.world_aabb)) {
                continue;
            }
            if (textured_renderers_[i]) {
                if (profiler_) {
                    profiler_->BeginPhase(n64::FrameProfiler::kPhaseTextureUpload);
                    textured_renderers_[i]->Draw();
                    profiler_->EndPhase(n64::FrameProfiler::kPhaseTextureUpload);
                } else {
                    textured_renderers_[i]->Draw();
                }
            }
        }
        return;
    }

    // Global near-pass material grouping (Inc 4). Build the per-frame triple
    // list (material, cell, first_run, run_count) over the visible textured
    // cells, sort it by material, then upload each sprite once and replay each
    // cell's geometry under that material. The triple list is a small static
    // buffer (9 cells × 32 materials = 288 × 8 B = 2.3 KB) — the plan's SF3
    // allows a dedicated static buffer when FrameArena headroom is tight.
    static NearMaterialTriple s_triples[288];
    int triple_count = 0;
    for (int i = 0; i < set_.count; ++i) {
        const V2RoomSpec& rs = *set_.spec[i];
        if (!CellAabbInNearCone(cam.pos, cam.target, cam.fov_deg, cam.near,
                                cam.far, rs.world_aabb)) {
            continue;
        }
        TexturedRoomRenderer* tr = textured_renderers_[i];
        if (!tr) continue;
        const int gc = tr->MaterialGroupCount();
        for (int g = 0; g < gc; ++g) {
            if (triple_count >= 288) break;  // safety net (should not happen)
            uint16_t mat = 0;
            int first_run = 0, run_count = 0;
            tr->MaterialGroupAt(g, &mat, &first_run, &run_count);
            s_triples[triple_count].material_id = mat;
            s_triples[triple_count].cell_index = (int16_t)i;
            s_triples[triple_count].first_run = (uint16_t)first_run;
            s_triples[triple_count].run_count = (uint16_t)run_count;
            ++triple_count;
        }
    }
    if (triple_count == 0) return;
    SortMaterialTriplesByMaterial(s_triples, triple_count);

    // Replay: for each material, upload the sprite once, then draw each cell's
    // runs of that material. The whole textured near pass is measured under
    // kPhaseTextureUpload (the sprite uploads are the dominant cost).
    if (profiler_) {
        profiler_->BeginPhase(n64::FrameProfiler::kPhaseTextureUpload);
    }
    int m = 0;
    while (m < triple_count) {
        const uint16_t mat = s_triples[m].material_id;
        // Upload the sprite ONCE for this material (from the first cell that
        // has it — the catalog is shared, so any cell resolves the same sprite).
        textured_renderers_[s_triples[m].cell_index]->UploadMaterial(mat, counters_);
        // Replay every cell's runs of this material under the uploaded state.
        while (m < triple_count && s_triples[m].material_id == mat) {
            const NearMaterialTriple& t = s_triples[m];
            textured_renderers_[t.cell_index]->DrawMaterialRun(
                t.first_run, t.run_count, counters_);
            ++m;
        }
    }
    if (profiler_) {
        profiler_->EndPhase(n64::FrameProfiler::kPhaseTextureUpload);
    }
}

}  // namespace madeline_cube
