#include "gameplay/render/tile_streamer.hpp"

#include <cstdio>
#include <cstring>

#include <rdpq.h>
#include <rdpq_mode.h>

#include "gameplay/render/lvl_room_renderer.hpp"
#include "gameplay/render/open_world_renderer.hpp"  // RenderCounters (Inc 1 / D7)
#include "gameplay/render/lod_math.hpp"             // CellAabbInNearCone
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
    return SetCenterImpl(spec, center, Vec3{0.0f, 0.0f, 0.0f}, build_dir);
}

bool TileStreamer::SetCenter(const MapSpecV2& spec, const V2RoomSpec& center,
                             const Vec3& camera_dir, const char* build_dir) {
    return SetCenterImpl(spec, center, camera_dir, build_dir);
}

bool TileStreamer::SetCenterImpl(const MapSpecV2& spec, const V2RoomSpec& center,
                                 const Vec3& camera_dir, const char* build_dir) {
    // Resolve the resident set. With a camera direction we load a forward
    // wedge (more cells in front, none behind); without one we fall back to
    // the old square ring for compatibility.
    const V2RoomSpec* ring[kMaxRing] = {};
    const int count = ResolveForwardWedge(spec, center, camera_dir, ring, kMaxRing);
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
    // loaded one (last_used = 0). Snapshot the ENTIRE old resident state
    // (spec pointers + renderers + last_used) before the in-place rewrite so
    // the kept-cell lookup never reads a slot that has already been clobbered.
    //
    // BUG FIX: the old code called `set_.IndexOf(s)` against the LIVE
    // set_.spec[] while `set_.spec[n] = s` was overwriting it in place. The new
    // center is ring[0] and is written to set_.spec[0] (the OLD center's slot)
    // on the first iteration, so when the loop later reaches the old center
    // (now a kept neighbor at ring[i>0]), IndexOf fails, the old center is
    // treated as a brand-new cell, is not in diff.load, and is silently
    // DROPPED. That drops the cell you just left (missing/broken geometry) and
    // orphans its renderer (heap leak -> exhaustion -> mid-map crash).
    // Searching the immutable snapshot fixes both.
    const V2RoomSpec* old_spec[kMaxRing] = {};
    LvlRoomRenderer* old_flat[kMaxRing] = {};
    TexturedRoomRenderer* old_tex[kMaxRing] = {};
    uint32_t old_last_used[kMaxRing] = {};
    for (int k = 0; k < set_.count; ++k) {
        old_spec[k] = set_.spec[k];
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
        // Look up the kept cell against the SNAPSHOT, not the live set_.spec[]
        // (which is being rewritten in place below).
        int old_idx = -1;
        for (int k = 0; k < set_.count; ++k) {
            if (old_spec[k] == s) { old_idx = k; break; }
        }
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

bool TileStreamer::UpdateCamera(const CameraDesc& cam) {
    // Per-frame resident visibility. We resolve which loaded resident cells
    // are actually inside the camera cone and draw only those. The AABB-cone
    // predicate widens the cone by atan(half_diag/dist) per corner, so a cell
    // cannot pass through the frustum with all four corners outside.
    visibility_.Clear();

    // First run the over-capacity eviction safety net (currently unreachable
    // because the wedge never exceeds kMaxRing, but preserved for robustness).
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

    // Build the visibility mask. With only a 3×3 resident ring, the per-cell
    // AABB-cone cull is disabled; we draw all resident cells. This avoids
    // screen-edge pop when a cell's AABB corner misses the widened cone while
    // part of the cell is still on-screen. The ring size is the budget.
    for (int i = 0; i < set_.count; ++i) {
        visibility_.Set(i, true);
    }

    return true;
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

void TileStreamer::DrawHighPriority(const CameraDesc& cam) {
    // Draw exactly the resident cells whose AABB intersects the camera cone
    // (AABB-cone test, no grid-index cut). The mask is built once per frame by
    // `UpdateCamera`; an AABB-cone test cannot cut mid-cell.

    // The RDP depth test/write must be ON before any near geometry is emitted.
    // T3D_FLAG_DEPTH only selects zbuf-capable triangle commands; forcing
    // rdpq_mode_zbuf here guarantees SOM_Z_COMPARE/SOM_Z_WRITE are set even if
    // a prior phase left them disabled.
    rdpq_sync_pipe();
    rdpq_mode_zbuf(true, true);

    // The near pass is fully opaque (the depth buffer resolves order), so
    // draws can be reordered by material without changing the visible result.
    // We group the near pass per-material → per-cell so each TMEM sprite is
    // uploaded ONCE per material instead of once per (material, cell). The
    // flat (untextured) fallback renderers are drawn per-cell as before.

    // Pass 1: flat (untextured) fallback renderers, per-cell as before.
    for (int i = 0; i < set_.count; ++i) {
        if (!visibility_.visible[i]) continue;
        if (renderers_[i]) {
            renderers_[i]->Draw();
        }
    }

    // Pass 2: textured renderers, globally grouped by material.
    // Build the per-frame triple list (material, cell, first_run, run_count)
    // over the visible textured cells, sort it by material, then upload each
    // sprite once and replay each cell's geometry under that material. The
    // triple list is a small static buffer (9 cells × 32 materials = 288 × 8 B
    // = 2.3 KB) — the plan's SF3 allows a dedicated static buffer when
    // FrameArena headroom is tight.
    static NearMaterialTriple s_triples[288];
    int triple_count = 0;
    for (int i = 0; i < set_.count; ++i) {
        if (!visibility_.visible[i]) continue;
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
