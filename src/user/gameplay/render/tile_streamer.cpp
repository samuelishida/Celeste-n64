#include "gameplay/render/tile_streamer.hpp"

#include <cstdio>
#include <cstring>

#include <rdpq.h>
#include <rdpq_mode.h>

#include "gameplay/render/lvl_room_renderer.hpp"
#include "gameplay/render/open_world_renderer.hpp"  // RenderCounters (Inc 1 / D7)
#include "gameplay/render/lod_math.hpp"             // CellAabbInNearCone
#include "gameplay/render/textured_room_renderer.hpp"
#include "gameplay/debug_flags.hpp"  // kEnableCellReveal / kRevealDurationSec (Inc 4)
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
    // streaming-experience Inc 1: free the async staging buffer. If a DMA is
    // still in flight (a pending/abandoned ticket), wait for it to drain
    // BEFORE freeing the buffer — otherwise the PI engine writes into freed
    // RDRAM. Bounded: one small ROM read (<= the largest cell, ~93 KB) at PI
    // speed completes in well under a frame.
    if (staging_) {
        const int inflight = pending_.FindInFlight();
        if (inflight >= 0) dma_wait_finished(pending_.slots[inflight].ticket);
        free_uncached(staging_);
        staging_ = nullptr;
        staging_size_ = 0;
    }
    pending_ = PendingLoadQueue{};
}

void TileStreamer::SetAsyncStagingPool(uint8_t* pool, uint32_t pool_size) {
    // Take ownership of the caller's pool (Inc 2 reserves it from
    // `MapSpecV2::MaxLvlSize()` + margin). If a fallback buffer was already
    // lazily allocated, free it first (the pool supersedes it).
    if (staging_ && staging_ != pool) {
        const int inflight = pending_.FindInFlight();
        if (inflight >= 0) dma_wait_finished(pending_.slots[inflight].ticket);
        free_uncached(staging_);
    }
    staging_ = pool;
    staging_size_ = pool_size;
}

bool TileStreamer::ReserveStreamingBudget(uint32_t max_lvl_size) {
    // streaming-experience Inc 2: pre-reserve the streaming heap budget at
    // init. Allocates ONE contiguous uncached staging pool sized from the
    // manifest's largest cell (+ margin) so a whole-file DMA read of any cell
    // fits, with no fragmentation. Idempotent — only reserves once. On failure
    // the Inc 1 per-load `malloc_uncached(kStagingFallbackBytes)` fallback
    // stays active (IssueAsyncRead lazily allocates it).
    if (staging_) return true;  // already reserved (or a pool is attached)
    const uint32_t pool_size = max_lvl_size + kStagingMargin;
    uint8_t* pool = static_cast<uint8_t*>(malloc_uncached(pool_size));
    if (!pool) {
        debugf("[streamer] ReserveStreamingBudget FAILED (%u B) — using fallback\n",
               (unsigned int)pool_size);
        return false;
    }
    staging_ = pool;
    staging_size_ = pool_size;
    return true;
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
    // Resolve the resident set. streaming-experience Inc 3: the RESIDENT ring
    // is the 5×5 (Chebyshev distance 2, forward-biased for load order) — the
    // preload pool. The DRAWN set stays exactly the 3×3 (cheb≤1), resolved in
    // `UpdateCamera`'s mask pass. With a camera direction we load a forward
    // wedge (more cells in front, none behind); without one we fall back to
    // the old square ring for compatibility.
    const V2RoomSpec* ring[kMaxResident] = {};
    const int count = ResolveForwardWedge(spec, center, camera_dir, ring,
                                           kMaxResident, /*max_cheb=*/2);
    if (count == 0) return false;

    // streaming-memory-opt Inc 1: diff the new ring against the current
    // residents instead of free-all + load-all. A center→neighbor transition
    // keeps the overlapping cells (no reload, no RSPQ recompile) and only
    // loads the new cells / frees the ones that left. This kills the primary
    // streaming hitch (Decision 1).
    const RingDiffResult diff = ResolveRingDiff(set_, ring, count);

    // Free every resident (both flat and textured) and reset the pool. Used on
    // a center load failure to match the old "reset the ring" behavior without
    // leaking the kept residents. Also drops any pending async-load records.
    auto free_all = [this]() {
        for (int k = 0; k < set_.count; ++k) {
            DropPending(set_.spec[k]);
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

    // Free the residents that left the ring (exactly once each). Drop any
    // pending async-load record for them (streaming-experience Inc 1).
    for (int i = 0; i < diff.free_count; ++i) {
        const int k = set_.IndexOf(diff.free[i]);
        if (k < 0) continue;  // defensive: already gone
        DropPending(set_.spec[k]);
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

    // streaming-experience Inc 1: load the new cells. The center (ring[0])
    // loads SYNCHRONOUSLY — it must be READY before it draws (a not-ready
    // center would be a 240-world-unit hole around the player). Non-center
    // cells load ASYNC when the textured path + staging pool are available:
    // enqueue into the pending queue (the pump issues the DMA + parses it one
    // per frame); the cell enters the resident set immediately with a null
    // renderer slot so the ring diff and the center-never-evict rule work on
    // it, and `DrawHighPriority` naturally skips it (null renderer — the same
    // path a load failure takes today, so no new draw guard is needed).
    //
    // Side tables are keyed by SPEC POINTER (not load index) so a load failure
    // (a null hole) can never be picked up by an off-by-one index probe — the
    // latent bug the review flagged (Inc 1 SHOULD-FIX).
    struct LoadRecord {
        const V2RoomSpec* spec = nullptr;
        LvlRoomRenderer* flat = nullptr;
        TexturedRoomRenderer* tex = nullptr;
    };
    LoadRecord loaded[kMaxResident] = {};
    int loaded_count = 0;
    const bool async_ok = kEnableTextures && catalog_ && staging_ != nullptr;
    // streaming-experience Inc 5: no per-transition load cap — every new ring
    // cell gets its load work in THIS call. The center still loads
    // synchronously (it must be ready before it draws); non-center cells are
    // enqueued into the pending queue (the 128-slot backlog). The pump drains
    // one DMA + one parse per frame, so a fresh 25-cell boot backlog goes
    // READY over ~25-50 frames — before the player can reach cells that are
    // 2 cells out (the reveal runway). Per-frame work stays bounded by the
    // pump, not by queue size; the frame-spike the old cap guarded against
    // never existed once the pump was per-frame.
    for (int i = 0; i < diff.load_count; ++i) {
        const V2RoomSpec& rs = *diff.load[i];
        const bool is_center = (&rs == &center);
        const char* path = LocalizePath(rs.lvl_path, build_dir);

        if (kEnableTextures && catalog_) {
            if (is_center) {
                // Center: synchronous, fatal on failure (reset + false).
                TexturedRoomRenderer* tr = new TexturedRoomRenderer();
                if (!tr->Load(path, rs.render_origin, catalog_)) {
                    delete tr;
                    free_all();
                    return false;
                }
                tr->SetCounters(counters_);
                loaded[loaded_count++] = {&rs, nullptr, tr};
            } else if (async_ok && pending_.Enqueue(&rs)) {
                // Async: enqueued; the pump issues the DMA + parses it. The
                // cell is added to the resident set below with a null renderer
                // (promoted by the pump when READY). No renderer is created
                // here — the pump's PromoteStaged builds it from the staged
                // bytes.
                loaded[loaded_count++] = {&rs, nullptr, nullptr};
            } else {
                // Async unavailable (no pool / queue full): synchronous fallback.
                TexturedRoomRenderer* tr = new TexturedRoomRenderer();
                if (!tr->Load(path, rs.render_origin, catalog_)) {
                    delete tr;
                    if (is_center) { free_all(); return false; }
                    continue;  // non-center failure: skip (existing behavior)
                }
                tr->SetCounters(counters_);
                loaded[loaded_count++] = {&rs, nullptr, tr};
            }
        } else {
            // Flat-color fallback path (kEnableTextures off / no catalog):
            // synchronous only (the async split is for the textured renderer).
            LvlRoomRenderer* r = new LvlRoomRenderer();
            if (!r->Load(path, rs.render_origin)) {
                delete r;
                if (is_center) { free_all(); return false; }
                continue;
            }
            r->SetCounters(counters_);
            loaded[loaded_count++] = {&rs, r, nullptr};
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
    const V2RoomSpec* old_spec[kMaxResident] = {};
    LvlRoomRenderer* old_flat[kMaxResident] = {};
    TexturedRoomRenderer* old_tex[kMaxResident] = {};
    uint32_t old_last_used[kMaxResident] = {};
    float old_reveal_scale[kMaxResident] = {};
    bool old_revealing[kMaxResident] = {};
    for (int k = 0; k < set_.count; ++k) {
        old_spec[k] = set_.spec[k];
        old_flat[k] = renderers_[k];
        old_tex[k] = textured_renderers_[k];
        old_last_used[k] = set_.last_used[k];
        old_reveal_scale[k] = reveal_scale_[k];
        old_revealing[k] = revealing_[k];
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
            // Kept cell: reuse the existing renderer + last_used + reveal state.
            flat = old_flat[old_idx];
            tex = old_tex[old_idx];
            last_used = old_last_used[old_idx];
            reveal_scale_[n] = old_reveal_scale[old_idx];
            revealing_[n] = old_revealing[old_idx];
        } else {
            // Loaded cell: look up by SPEC POINTER (identity), never by load
            // index — a failed load leaves a null hole that an index probe
            // could silently pick up (Inc 1 SHOULD-FIX).
            for (int li = 0; li < loaded_count; ++li) {
                if (loaded[li].spec == s) {
                    flat = loaded[li].flat;
                    tex = loaded[li].tex;
                    break;
                }
            }
            // A non-center load that failed is skipped: flat/tex stay null and
            // the cell is simply not added (existing behavior). A PENDING
            // (async) cell has a null renderer but IS tracked in the pending
            // queue — it must still enter the resident set so the ring diff
            // and the center-never-evict rule work on it, and the pump
            // promotes it when READY.
            if (!flat && !tex && !pending_.Tracks(s)) continue;
            // New cell: reset reveal to fully-revealed (UpdateCamera re-triggers
            // a reveal if it enters the drawn set while still loading).
            ResetReveal(n);
        }
        renderers_[n] = flat;
        textured_renderers_[n] = tex;
        set_.spec[n] = s;
        set_.last_used[n] = last_used;
        ++n;
    }
    // Clear any trailing slots beyond the new count (stale moved-from / freed
    // pointers) so the destructor never double-frees.
    for (int k = n; k < kMaxResident; ++k) {
        renderers_[k] = nullptr;
        textured_renderers_[k] = nullptr;
        set_.spec[k] = nullptr;
        set_.last_used[k] = 0;
        ResetReveal(k);
    }
    set_.count = n;
    return n > 0;
}

uint64_t TileStreamer::IssueAsyncRead(const V2RoomSpec* spec) {
    // streaming-experience Inc 1: issue a non-blocking DMA read of the cell's
    // .lvl into the shared staging buffer. Returns the ticket (non-zero) or 0
    // on failure (caller falls back to a synchronous Load).
    //
    // Inc 1 precedes Inc 2's manifest-sized pool, so lazily allocate a single
    // `malloc_uncached(kStagingFallbackBytes)` fallback on first use (>= the
    // current manifest's largest cell, 93 KB). Inc 2's `ReserveStreamingBudget`
    // attaches a manifest-sized pool via `SetAsyncStagingPool`, superseding it.
    if (!staging_) {
        staging_ = static_cast<uint8_t*>(malloc_uncached(kStagingFallbackBytes));
        staging_size_ = staging_ ? kStagingFallbackBytes : 0;
    }
    if (!staging_ || spec->lvl_size == 0) return 0;
    uint64_t ticket = 0;
    if (!TexturedRoomRenderer::LoadAsync(spec->lvl_path, spec->lvl_size,
                                         staging_, staging_size_, &ticket)) {
        return 0;
    }
    return ticket;
}

bool TileStreamer::PromoteStaged(const V2RoomSpec* spec) {
    // streaming-experience Inc 1: parse the staged bytes for `spec` into a
    // fresh TexturedRoomRenderer and place it into the resident slot. Called
    // by the pump once the DMA for those bytes has completed. Returns true on
    // success (the cell is now READY and drawn).
    const int slot = set_.IndexOf(spec);
    if (slot < 0) return false;  // cell was evicted while in-flight
    TexturedRoomRenderer* tr = new TexturedRoomRenderer();
    if (!tr->ParseStaged(staging_, spec->lvl_size, spec->render_origin,
                         catalog_)) {
        delete tr;
        return false;
    }
    tr->SetCounters(counters_);
    // Free any stale renderer in the slot (shouldn't happen — a pending cell
    // has a null slot — but be safe).
    if (textured_renderers_[slot]) {
        textured_renderers_[slot]->Free();
        delete textured_renderers_[slot];
    }
    textured_renderers_[slot] = tr;
    return true;
}

bool TileStreamer::LoadSync(const V2RoomSpec& spec, const char* build_dir) {
    // streaming-experience Inc 1: synchronous fallback for `spec` (center at
    // boot, async-pool exhaustion, DMA issue failure, queue full, timeout).
    // Returns true if the cell is resident with a renderer afterwards.
    const int slot = set_.IndexOf(&spec);
    if (slot < 0) return false;
    if (textured_renderers_[slot] || renderers_[slot]) return true;  // already ready
    const char* path = LocalizePath(spec.lvl_path, build_dir);
    if (kEnableTextures && catalog_) {
        TexturedRoomRenderer* tr = new TexturedRoomRenderer();
        if (!tr->Load(path, spec.render_origin, catalog_)) {
            delete tr;
            return false;
        }
        tr->SetCounters(counters_);
        textured_renderers_[slot] = tr;
        return true;
    }
    LvlRoomRenderer* r = new LvlRoomRenderer();
    if (!r->Load(path, spec.render_origin)) {
        delete r;
        return false;
    }
    r->SetCounters(counters_);
    renderers_[slot] = r;
    return true;
}

void TileStreamer::RequeueResidentLoads() {
    // streaming-experience Inc 5: the pump is the single owner of "make not-
    // ready residents ready". Enqueue any resident cell whose renderer slot is
    // still null and is not already tracked: (a) the whole boot backlog the
    // SetCenter loop enqueued but the pump has not drained yet (no-op, they
    // are tracked), (b) cells whose async parse failed and were re-offered
    // by the pump, (c) queue-full stragglers from SetCenter. The queue (128
    // slots) is the backlog; the pump drains it one DMA + one parse per
    // frame. Idempotent — Enqueue dedups against every tracked spec.
    if (!(kEnableTextures && catalog_ && staging_ != nullptr)) return;
    for (int i = 0; i < set_.count; ++i) {
        if (set_.spec[i] == nullptr) continue;
        if (textured_renderers_[i] || renderers_[i]) continue;
        if (!pending_.Tracks(set_.spec[i])) {
            pending_.Enqueue(set_.spec[i]);  // may fail (full) — retried next frame
        }
    }
}

void TileStreamer::PumpAsyncLoads() {
    // streaming-experience Inc 1: per-frame async-load pump. Runs at the top
    // of Update (BeginFrame), before SetCenter issues new loads. Bounded work:
    // at most one parse per frame, at most one DMA issue per frame, and the
    // in-flight ticket is polled (never blocking).
    ++pump_frame_;

    // 0) Backlog re-enqueue (streaming-experience Inc 5): re-offer any
    // resident-but-not-ready-and-not-tracked cells to the pending queue so
    // the full 5×5 ring always gets a loading shot (boot backlog, parse-fail
    // retries, queue-full stragglers). Idempotent; O(resident×slots).
    RequeueResidentLoads();

    // 1) If a DMA is in flight, poll it. On completion, parse the cell (at
    //    most one per frame) and promote it; on timeout, fall back to sync.
    const int inflight = pending_.FindInFlight();
    if (inflight >= 0) {
        PendingLoadQueue::Slot& s = pending_.slots[inflight];
        const bool done = dma_finished(s.ticket);
        const bool timed_out =
            (pump_frame_ - s.issued_frame) > PendingLoadQueue::kPendingTimeoutFrames;
        if (done || timed_out) {
            if (s.state == PendingLoadQueue::kInFlight && !timed_out) {
                // DMA completed: parse + promote. On failure the cell STAYS
                // resident with a null renderer and is retried async next
                // frame via the backlog re-enqueue (streaming-experience
                // Inc 5) — bounded, since the pump issues one DMA per frame.
                // Keeping the resident slot in place also keeps the per-slot
                // reveal arrays index-aligned.
                //
                // The center is the only cell that MUST be ready before it
                // draws (a not-ready center would be a 240-unit hole around
                // the player), so it gets an immediate synchronous fallback;
                // if that fails too, the backlog re-enqueue keeps retrying.
                if (!PromoteStaged(s.spec)) {
                    if (set_.count > 0 && set_.spec[0] == s.spec) {
                        LoadSync(*s.spec, /*build_dir=*/nullptr);
                    }
                }
            } else {
                // Timed out (or abandoned): the DMA never completed in a
                // reasonable time. Fall back to a synchronous load for the
                // cell, but only if it is still resident AND still null — a
                // READY resident would be double-loaded (and leak its
                // renderer), and a non-resident spec needs nothing. The
                // backlog re-enqueue retries the cell async next frame.
                if (s.state == PendingLoadQueue::kInFlight) {
                    const int t = set_.IndexOf(s.spec);
                    if (t >= 0 && !renderers_[t] && !textured_renderers_[t]) {
                        LoadSync(*s.spec, /*build_dir=*/nullptr);
                    }
                }
            }
            pending_.FreeSlot(inflight);
        }
    }

    // 2) If the staging slot is free and a cell is queued, issue its DMA.
    if (pending_.FindInFlight() < 0) {
        const int next = pending_.FirstQueued();
        if (next >= 0) {
            const uint64_t ticket = IssueAsyncRead(pending_.slots[next].spec);
            if (ticket != 0) {
                pending_.MarkIssued(next, ticket, pump_frame_);
            } else {
                // Issue failure: fall back to sync for this cell, drop it.
                LoadSync(*pending_.slots[next].spec, /*build_dir=*/nullptr);
                pending_.FreeSlot(next);
            }
        }
    }
}

bool TileStreamer::UpdateCamera(const CameraDesc& cam, float dt) {
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
            // streaming-experience Inc 1: drop any pending async-load record
            // for the evicted cell (its DMA, if in flight, drains harmlessly).
            DropPending(set_.spec[victim]);
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
                // streaming-experience Inc 5: keep the per-slot reveal arrays
                // aligned with set_.spec[] (the SetCenterImpl compaction does
                // this via the old_* snapshot; this one is a per-victim
                // shift). Then clear the stale tail slot.
                reveal_scale_[k] = reveal_scale_[k + 1];
                revealing_[k] = revealing_[k + 1];
            }
            ResetReveal(set_.count - 1);
            --set_.count;
            ++evicted_this_frame_;
        }
    }

    // Build the visibility mask. streaming-experience Inc 3: the mask is the
    // DRAWN set — exactly the 3×3 centered on the active cell (Chebyshev
    // distance ≤ 1), independent of `kEnableNearCulling`. Preloaded outer
    // (cheb=2) residents get `visible=false` (never drawn); the center is
    // always visible (guard). This is the always-on rule that keeps the drawn
    // boundary at the 3×3 — drawing preloaded cells would move the visible
    // boundary outward and re-import the screen-edge pop the `kEnableNearCulling`
    // gate exists to avoid.
    //
    // `kEnableNearCulling` (default OFF) gates ONLY the per-cell AABB-cone
    // cull test (a future feature); it does NOT change the drawn 3×3 mask.
    const V2RoomSpec* center = (set_.count > 0) ? set_.spec[0] : nullptr;
    for (int i = 0; i < set_.count; ++i) {
        bool visible = true;
        if (center && i != 0) {
            visible = IsDrawnCell(*set_.spec[i], *center);
            if (kEnableNearCulling && visible) {
                visible = CellAabbInNearCone(cam.pos, cam.target, cam.fov_deg,
                                             cam.near, cam.far,
                                             set_.spec[i]->world_aabb);
            }
        }
        visibility_.Set(i, visible);

        // streaming-experience Inc 4: reveal state machine. A cell that is
        // DRAWN while its renderer is still null (a late load) starts revealing
        // from epsilon; the scale advances toward 1.0 once the renderer is
        // READY (so the animation is timed to actual appearance). A cell that
        // is READY+drawn from its first drawn frame (the normal preloaded case)
        // stays at 1.0 (instant). A cell that leaves the drawn set resets.
        // Gated by `kEnableCellReveal`; when OFF, all cells stay at 1.0 (the
        // byte-identical today path).
        if (kEnableCellReveal) {
            if (visible) {
                const bool ready = (textured_renderers_[i] != nullptr) ||
                                   (renderers_[i] != nullptr);
                if (!ready) {
                    // Late load: (re)start the reveal at epsilon; it advances
                    // once the renderer is ready.
                    revealing_[i] = true;
                    reveal_scale_[i] = kRevealEpsilon;
                } else if (revealing_[i]) {
                    // Geometry present, reveal in progress → advance.
                    reveal_scale_[i] += dt / kRevealDurationSec;
                    if (reveal_scale_[i] >= 1.0f) {
                        reveal_scale_[i] = 1.0f;
                        revealing_[i] = false;
                    }
                }
                // else: ready & not revealing & scale==1 → instant, leave.
            } else {
                // Left the drawn set: reset (fresh on re-entry).
                revealing_[i] = false;
                reveal_scale_[i] = 1.0f;
            }
        }
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
    // triple list is a small static buffer sized for the RESIDENT pool
    // (kMaxResident × 32 materials = 800 × 8 B = 6.4 KB). With the drawn mask
    // it actually holds ≤ 9 cells (the mask caps drawing), but the safety-net
    // `break` below silently drops geometry if exceeded, so it is sized for
    // kMaxResident so the invariant is "never" regardless of mask regressions.
    static NearMaterialTriple s_triples[kMaxResident * 32];
    int triple_count = 0;
    for (int i = 0; i < set_.count; ++i) {
        if (!visibility_.visible[i]) continue;
        TexturedRoomRenderer* tr = textured_renderers_[i];
        if (!tr) continue;
        const int gc = tr->MaterialGroupCount();
        for (int g = 0; g < gc; ++g) {
            if (triple_count >= kMaxResident * 32) break;  // safety net
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
            // streaming-experience Inc 4: pass the cell's reveal scale so the
            // renderer composes the pivot-scale into the pushed matrix.
            textured_renderers_[t.cell_index]->DrawMaterialRun(
                t.first_run, t.run_count, counters_, RevealScale(t.cell_index));
            ++m;
        }
    }
    if (profiler_) {
        profiler_->EndPhase(n64::FrameProfiler::kPhaseTextureUpload);
    }
}

}  // namespace madeline_cube
