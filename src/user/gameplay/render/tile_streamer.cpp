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

    // Free all current residents (both flat and textured).
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

    // Resolve the distance ring (center + Chebyshev-1 neighbors).
    const V2RoomSpec* ring[kMaxRing] = {};
    const int count = ResolveDistanceRing(spec, center, ring, kMaxRing);
    if (count == 0) return false;

    for (int i = 0; i < count; ++i) {
        const V2RoomSpec& rs = *ring[i];
        const char* path = LocalizePath(rs.lvl_path, build_dir);
        if (kEnableTextures && catalog_) {
            TexturedRoomRenderer* tr = new TexturedRoomRenderer();
            if (!tr->Load(path, rs.render_origin, catalog_)) {
                delete tr;
                if (i == 0) { set_.count = 0; return false; }
                continue;
            }
            tr->SetCounters(counters_);  // Inc 1 / D7
            textured_renderers_[set_.count] = tr;
        } else {
            LvlRoomRenderer* r = new LvlRoomRenderer();
            if (!r->Load(path, rs.render_origin)) {
                delete r;
                if (i == 0) { set_.count = 0; return false; }
                continue;
            }
            r->SetCounters(counters_);  // Inc 1 / D7
            renderers_[set_.count] = r;
        }
        set_.spec[set_.count] = &rs;
        set_.last_used[set_.count] = 0;
        ++set_.count;
    }
    return set_.count > 0;
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

void TileStreamer::DrawHighPriority(const CameraDesc&) {
    // Inc 4 / D4 + z-split fix: draw ALL residents every frame. The resident
    // pool is already bounded to the near ring (center + Chebyshev-1, ≤9
    // cells), so drawing them all has no memory/streaming cost. The per-frame
    // frustum visibility gate was removed because it culls by whole grid cell,
    // but a brush is assigned to a cell by its center and can visually
    // overflow into a neighbor cell — so a neighbor's geometry can still be on
    // screen even when the frustum-on-ground polygon stops touching that
    // cell's grid index, producing a straight cut at the cell boundary when
    // the camera rotates. Drawing the whole (small) ring eliminates the cut
    // with negligible cost.
    for (int i = 0; i < set_.count; ++i) {
        if (textured_renderers_[i]) {
            // Inc 1 / D7: measure the textured near-pass draw (which includes
            // the TMEM sprite uploads) under kPhaseTextureUpload so the upload
            // cost is isolated from the high-priority pass total. The phase is
            // per resident so one slow cell is visible in the report.
            if (profiler_) {
                profiler_->BeginPhase(n64::FrameProfiler::kPhaseTextureUpload);
                textured_renderers_[i]->Draw();
                profiler_->EndPhase(n64::FrameProfiler::kPhaseTextureUpload);
            } else {
                textured_renderers_[i]->Draw();
            }
        } else if (renderers_[i]) {
            renderers_[i]->Draw();
        }
    }
}

}  // namespace madeline_cube
