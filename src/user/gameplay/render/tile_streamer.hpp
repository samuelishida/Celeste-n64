#pragma once

#include <cstddef>
#include <cstdint>

#include "gameplay/math_types.hpp"
#include "gameplay/render/pass_camera_math.hpp"
#include "gameplay/render/render_origin_math.hpp"  // ResolveCellIndex (canonical)
#include "gameplay/render/tile_visibility.hpp"      // Mat4, ProjectFrustumToGround, ScanlineTileRanges
#include "gameplay/world/mappack_loader.hpp"        // MapSpecV2, V2RoomSpec

namespace n64 {
class FrameProfiler;  // defined in n64/profiler.hpp (Inc 1 / D7)
class FrameArena;     // defined in n64/frame_arena.hpp (Inc 5 / D6)
}  // namespace n64

namespace madeline_cube {

class LvlRoomRenderer;
class TexturedRoomRenderer;
class MaterialCatalog;
struct RenderCounters;  // defined in open_world_renderer.hpp (Inc 1 / D7)

// Texturing gate (Inc 5). When true, the near pass uses `TexturedRoomRenderer`
// (per-material sprites); when false, it uses the flat-color `LvlRoomRenderer`
// (the validated fallback). Defaults ON; can be disabled if RDP state-change
// cost is too high.
inline constexpr bool kEnableTextures = true;

// Near-pass cone-cull gate (n64-optimization Inc 3). When true,
// `TileStreamer::UpdateCamera` runs the `CellAabbInNearCone` AABB-cone test
// per resident cell and draws only the cells inside the (margined) camera
// cone; when false, every resident cell is force-drawn (the current
// behavior). Defaults OFF: with a 3×3 resident ring the cone is the only
// effective cull (the ring cells are all within far), and the screen-edge
// pop risk is the exact artifact class previously reverted (forward-wedge
// rationale, AGENTS.md). The cone width is controlled by `kCullMargin`
// (lod_math.hpp, 1.15f, hardcoded inside the predicate); widen it ±0.05 and
// re-test a full 360° orbit if a cell's AABB barely clips the cone corner.
// Rollback is a clean flag flip.
inline constexpr bool kEnableNearCulling = false;  // render-distance Inc 3: left OFF — house-scene A/B showed ~0 win (all 9 cells in-cone) and no-pop could not be verified with the forward-only autowalk; scene-dependent

// ── Resident-pool constants ──────────────────────────────────────────
// `kMaxRing` is the DRAWN-ring capacity: the center cell + its up-to-8 ring
// neighbors (Chebyshev distance 1 in the 2D XZ grid) — exactly the 3×3 that is
// drawn each frame. This is the same `kMaxVisibleCells` budget the plan caps
// the near pass at. It stays 9 (the drawn set is UNCHANGED by the preload
// ring; see `kMaxResident`).
constexpr int kMaxRing = 9;

// streaming-experience Inc 3: the RESIDENT-pool capacity — the 5×5 ring
// (Chebyshev distance 2) that is PRELOADED but not drawn. Preloaded-but-not-
// drawn cells exist purely to be READY before they enter the 3×3 drawn ring;
// they never render (the drawn mask is exactly the 3×3). Growing the resident
// pool from 9 to 25 means a 1-step move keeps 12 overlapping residents instead
// of 6 (fewer parses per transition) and late loads happen one whole cell
// away from the drawn boundary (more reveal runway).
constexpr int kMaxResident = 25;

// Resolve the near-pass resident set. The center cell is always loaded, plus
// all Chebyshev-1 neighbors (the 3×3 ring) that exist in the manifest. The
// 9-cell budget exactly matches `kMaxRing`, so no neighbor is dropped due to
// capacity. The camera direction is used only to prioritize the sort order
// (forward cells are placed first), not to cull side/back cells; this avoids
// the screen-edge gaps caused by an aggressive forward half-space cut.
//
// `camera_dir` is the normalized world-space XZ forward direction. It is used
// as a tie-breaker; pass a zero-length vector to disable prioritization. The
// legacy `ResolveDistanceRing` alias does exactly that. `out[0]` is the center;
// map-edge cells simply have fewer neighbors. Returns 1..kMaxRing.
// Host-safe — no N64 types.
inline int ResolveForwardWedge(const MapSpecV2& spec, const V2RoomSpec& center,
                               const Vec3& camera_dir,
                               const V2RoomSpec* out[], int out_capacity,
                               int max_cheb = 1) {
    if (!out || out_capacity <= 0) return 0;
    int n = 0;
    out[n++] = &center;

    const bool has_dir = camera_dir.x != 0.0f || camera_dir.z != 0.0f;

    struct Candidate {
        const V2RoomSpec* spec = nullptr;
        int cheb = 0;
        float dot = 0.0f;
    };
    Candidate candidates[64];
    int candidate_count = 0;

    const int cx = center.cell_ix;
    const int cz = center.cell_iz;
    for (int i = 0; i < spec.room_count; ++i) {
        const V2RoomSpec& r = spec.rooms[i];
        if (r.id[0] == '\0') continue;
        if (&r == &center) continue;
        const int dx = r.cell_ix - cx;
        const int dz = r.cell_iz - cz;
        if (dx == 0 && dz == 0) continue;

        const int cheb = (dx < 0 ? -dx : dx) > (dz < 0 ? -dz : dz)
                             ? (dx < 0 ? -dx : dx)
                             : (dz < 0 ? -dz : dz);
        if (cheb > max_cheb) continue;

        float dot = 0.0f;
        if (has_dir) {
            const float cell_size = spec.chunk_size * spec.scale;
            const float wx = dx * cell_size;
            const float wz = dz * cell_size;
            const float len = std::sqrt(wx * wx + wz * wz);
            if (len > 1e-6f) {
                dot = (wx * camera_dir.x + wz * camera_dir.z) / len;
            }
        }

        if (candidate_count < 64) {
            candidates[candidate_count++] = {&r, cheb, dot};
        }
    }

    // Sort by cheb ascending, then by dot descending so forward cells are placed
    // first. Because the 3×3 ring fits in kMaxRing, the sort only affects order,
    // not coverage.
    for (int a = 1; a < candidate_count; ++a) {
        const Candidate key = candidates[a];
        int b = a - 1;
        while (b >= 0 &&
               (candidates[b].cheb > key.cheb ||
                (candidates[b].cheb == key.cheb && candidates[b].dot < key.dot))) {
            candidates[b + 1] = candidates[b];
            --b;
        }
        candidates[b + 1] = key;
    }

    for (int i = 0; i < candidate_count && n < out_capacity; ++i) {
        out[n++] = candidates[i].spec;
    }
    return n;
}

// Backward-compatible alias: the old 3×3 Chebyshev distance-1 ring, with no
// camera-direction prioritization. Existing tests continue to work.
inline int ResolveDistanceRing(const MapSpecV2& spec, const V2RoomSpec& center,
                               const V2RoomSpec* out[], int out_capacity) {
    return ResolveForwardWedge(spec, center, Vec3{0.0f, 0.0f, 0.0f}, out,
                               out_capacity, /*max_cheb=*/1);
}

// Resolve the visible tile set from a top-view frustum projection. Projects
// the inverse view-projection to a 2D ground polygon, scanline-enumerates the
// tiles it covers (`arch.md` §14-15), and maps each tile index to a manifest
// room via the canonical `ResolveCellIndex`. Deduplicates and bounds the
// output by `out_capacity`. Returns the visible room count.
//
// `ground_y` is unused (kept for signature clarity — the XZ projection drops
// Y); `tile_size` must equal the world cell size (`spec.chunk_size*scale`).
// Host-safe — no N64 types.
inline int ResolveVisibleTiles(const MapSpecV2& spec,
                               const Mat4& inv_view_proj, float ground_y,
                               float tile_size,
                               const V2RoomSpec* out[], int out_capacity) {
    if (!out || out_capacity <= 0) return 0;
    const float cell = spec.chunk_size * spec.scale;
    if (cell <= 0.0f) return 0;
    const Polygon2 poly = ProjectFrustumToGround(inv_view_proj);
    int n = 0;
    ScanlineTileRanges(poly, tile_size, -100000, 100000,
                       [&](int iz, int x_min, int x_max) {
                           for (int ix = x_min; ix <= x_max && n < out_capacity;
                                ++ix) {
                               for (int i = 0; i < spec.room_count; ++i) {
                                   const V2RoomSpec& r = spec.rooms[i];
                                   if (r.id[0] == '\0') continue;
                                   if (r.cell_ix != ix || r.cell_iz != iz) continue;
                                   bool dup = false;
                                   for (int k = 0; k < n; ++k) {
                                       if (out[k] == &r) { dup = true; break; }
                                   }
                                   if (!dup) out[n++] = &r;
                                   break;
                               }
                           }
                       });
    (void)ground_y;
    return n;
}

// Host-safe resident-set bookkeeping + LRU eviction decision. The device
// `TileStreamer` pairs each entry with a `LvlRoomRenderer*`; this struct holds
// only the decision data (which cells are resident, last-used frame) so the
// LRU rules are host-testable via Pattern C tests.
struct ResidentSet {
    const V2RoomSpec* spec[kMaxResident] = {};
    uint32_t last_used[kMaxResident] = {};
    int count = 0;
    int capacity = kMaxResident;

    // Index of the given room, or -1 if not resident.
    int IndexOf(const V2RoomSpec* s) const {
        for (int i = 0; i < count; ++i) {
            if (spec[i] == s) return i;
        }
        return -1;
    }

    // Mark a resident as used at `frame`. No-op if not resident.
    void Touch(const V2RoomSpec* s, uint32_t frame) {
        const int i = IndexOf(s);
        if (i >= 0) last_used[i] = frame;
    }

    // Index of the least-recently-used resident that is NOT the center and
    // NOT in the drawn 3×3 (Chebyshev distance ≤ 1 from the center), or -1 if
    // every resident is drawn/center (nothing evictable). streaming-experience
    // Inc 3: the drawn 3×3 must never be evicted (it is what is on screen);
    // only preloaded outer (cheb=2) cells are evictable.
    int EvictCandidate(const V2RoomSpec* center) const {
        int best = -1;
        uint32_t best_frame = UINT32_MAX;
        for (int i = 0; i < count; ++i) {
            if (spec[i] == center) continue;  // never evict the center
            if (center) {
                const int dx = spec[i]->cell_ix - center->cell_ix;
                const int dz = spec[i]->cell_iz - center->cell_iz;
                const int ax = dx < 0 ? -dx : dx;
                const int az = dz < 0 ? -dz : dz;
                const int cheb = ax > az ? ax : az;
                if (cheb <= 1) continue;  // never evict the drawn 3×3
            }
            if (last_used[i] < best_frame) {
                best_frame = last_used[i];
                best = i;
            }
        }
        return best;
    }

    // True if the pool is over capacity (needs eviction).
    bool OverCapacity() const { return count > capacity; }
};

// Per-frame visibility mask for the resident pool. Host-safe: a small fixed-size
// bitmask so `TileStreamer` can resolve visibility once per frame and both the
// orchestrator (distant skip) and `DrawHighPriority` reuse the same decision.
struct ResidentVisibility {
    // `visible[i]` is true iff `set.spec[i]` is inside the camera frustum this
    // frame. The mask is recomputed every frame by `TileStreamer::UpdateCamera`.
    // streaming-experience Inc 3: sized to the RESIDENT pool (kMaxResident);
    // the mask's meaning becomes "drawn this frame" (the 3×3 = cheb≤1), NOT
    // "frustum-visible" (see the .cpp).
    bool visible[kMaxResident] = {};

    void Clear() {
        for (int i = 0; i < kMaxResident; ++i) visible[i] = false;
    }

    void Set(int i, bool v) {
        if (i >= 0 && i < kMaxResident) visible[i] = v;
    }
};

// streaming-memory-opt Inc 4: per-frame (material, cell, first_run, run_count)
// triple for the global near-pass material grouping. The near pass is fully
// opaque (depth buffer resolves order), so draws can be reordered by material
// without changing the visible result; each TMEM sprite is then uploaded ONCE
// per material instead of once per (material, cell). Host-safe (no N64 types).
struct NearMaterialTriple {
    uint16_t material_id;
    int16_t cell_index;
    uint16_t first_run;
    uint16_t run_count;
};

// streaming-memory-opt Inc 4: stable-sort a triple list by material_id
// (grouped), so all cells of one material are contiguous. Returns the count
// (unchanged). Host-safe.
inline int SortMaterialTriplesByMaterial(NearMaterialTriple* triples, int count) {
    for (int a = 1; a < count; ++a) {
        const NearMaterialTriple key = triples[a];
        int b = a - 1;
        while (b >= 0 && triples[b].material_id > key.material_id) {
            triples[b + 1] = triples[b];
            --b;
        }
        triples[b + 1] = key;
    }
    return count;
}

// streaming-memory-opt Inc 4: count the number of distinct materials in a
// triple list (== the number of sprite uploads per frame under the global
// near-pass material grouping). Host-safe.
inline int CountDistinctMaterials(const NearMaterialTriple* triples, int count) {
    int distinct = 0;
    for (int i = 0; i < count; ++i) {
        if (i == 0 || triples[i].material_id != triples[i - 1].material_id) {
            ++distinct;
        }
    }
    return distinct;
}

// Host-safe incremental ring diff (streaming-memory-opt Inc 1). Given the
// current resident set and the new ring, classify every cell:
//   - keep: resident AND in the new ring (its renderer is reused, no reload);
//   - load: in the new ring but not resident (a new renderer is loaded);
//   - free: resident but not in the new ring (its renderer is freed).
// `keep`/`load` are filled in new-ring order; `free` in resident order. The
// center is always new_ring[0]; if it is not resident it is classified as
// load (the .cpp treats a center load failure as fatal). Membership uses
// `ResidentSet::IndexOf` (pointer identity — the ring entries and the resident
// specs are both pointers into the same `MapSpecV2::rooms[]`). Host-safe — no
// N64 types; the device `TileStreamer` applies the result to its renderer
// arrays. This is what makes a center→neighbor transition load only the 1–3
// new cells instead of rebuilding all 9.
struct RingDiffResult {
    const V2RoomSpec* keep[kMaxResident] = {};
    const V2RoomSpec* load[kMaxResident] = {};
    const V2RoomSpec* free[kMaxResident] = {};
    int keep_count = 0;
    int load_count = 0;
    int free_count = 0;
};

inline RingDiffResult ResolveRingDiff(const ResidentSet& old_set,
                                      const V2RoomSpec* const new_ring[],
                                      int new_count) {
    RingDiffResult r;
    for (int i = 0; i < new_count; ++i) {
        const V2RoomSpec* s = new_ring[i];
        if (old_set.IndexOf(s) >= 0) r.keep[r.keep_count++] = s;
        else r.load[r.load_count++] = s;
    }
    for (int k = 0; k < old_set.count; ++k) {
        const V2RoomSpec* s = old_set.spec[k];
        bool in_new = false;
        for (int i = 0; i < new_count; ++i) {
            if (new_ring[i] == s) { in_new = true; break; }
        }
        if (!in_new) r.free[r.free_count++] = s;
    }
    return r;
}

// streaming-experience Inc 1: bounded pending-load state machine. Host-safe
// (Pattern A) so the issue→poll→parse→promote flow is testable with a mock
// `is_finished` callback; the device `TileStreamer` wires the real
// `dma_finished`/`dma_read_async` behind it.
//
// The queue holds up to `kMaxPending` cells that need their .lvl bytes read
// from ROM and parsed into a renderer. At most ONE DMA is in flight at a time
// (the shared staging buffer is single-slot), so the machine serializes: a
// cell is `kQueued` until the staging slot is free, then `kInFlight` while
// its DMA runs, then parsed (at most one parse per pump tick) and removed.
// A cell evicted from the resident pool while in-flight is `kAbandoned` — its
// DMA keeps draining the staging buffer harmlessly (bytes land, never
// parsed), and the slot frees when the ticket completes.
struct PendingLoadQueue {
    // Host-safe (the slots are POD). Device-only in effect. 128 is a hard
    // bound, not a per-frame budget: the pump still issues one DMA and parses
    // one cell per frame, so a full 25-cell boot backlog drains over ~25–50
    // frames. 128 comfortably holds the whole 5×5 ring plus the per-settle
    // re-enqueue backlog; slots are 20 B each (3.2 KB stack, host test only).
    static constexpr int kMaxPending = 128;
    static constexpr uint32_t kPendingTimeoutFrames = 180;  // ~6 s at 30 fps

    enum State : uint8_t {
        kFree = 0,
        kQueued = 1,     // waiting for the staging slot
        kInFlight = 2,    // DMA issued; bytes landing in the shared staging buffer
        kAbandoned = 3,   // cell evicted while in-flight; ticket still draining
    };

    struct Slot {
        const V2RoomSpec* spec = nullptr;
        uint64_t ticket = 0;        // non-zero while kInFlight / kAbandoned
        uint32_t issued_frame = 0;  // frame the DMA was issued (timeout bookkeeping)
        uint8_t state = kFree;
    };

    Slot slots[kMaxPending] = {};
    int count = 0;  // slots with state != kFree

    int IndexOf(const V2RoomSpec* s) const {
        for (int i = 0; i < kMaxPending; ++i)
            if (slots[i].state != kFree && slots[i].spec == s) return i;
        return -1;
    }
    bool Tracks(const V2RoomSpec* s) const { return IndexOf(s) >= 0; }

    int FreeSlotIndex() const {
        for (int i = 0; i < kMaxPending; ++i)
            if (slots[i].state == kFree) return i;
        return -1;
    }

    // Index of the single slot with a live ticket (kInFlight or kAbandoned),
    // or -1. There is at most one (the shared staging buffer).
    int FindInFlight() const {
        for (int i = 0; i < kMaxPending; ++i)
            if (slots[i].ticket != 0 && slots[i].state != kFree) return i;
        return -1;
    }

    // Index of the oldest kQueued slot (issue order), or -1.
    int FirstQueued() const {
        for (int i = 0; i < kMaxPending; ++i)
            if (slots[i].state == kQueued) return i;
        return -1;
    }

    // Add `s` to the queue. No duplicates: if `s` is already tracked (queued,
    // in-flight, or abandoned), do nothing (the dedup rule — a pending/READY
    // cell must not be enqueued twice; the rapid A→B→A→C regression). Returns
    // false only if the queue is full AND `s` is untracked (caller falls back
    // to a synchronous load).
    bool Enqueue(const V2RoomSpec* s) {
        if (Tracks(s)) return true;
        const int i = FreeSlotIndex();
        if (i < 0) return false;
        slots[i] = Slot{s, 0, 0, (uint8_t)kQueued};
        ++count;
        return true;
    }

    void MarkIssued(int i, uint64_t ticket, uint32_t frame) {
        slots[i].ticket = ticket;
        slots[i].issued_frame = frame;
        slots[i].state = kInFlight;
    }

    void FreeSlot(int i) {
        if (slots[i].state != kFree) --count;
        slots[i] = Slot{};
    }

    // Drop `s` on eviction. If it is in-flight, mark it kAbandoned (its DMA
    // keeps draining the staging buffer; the slot frees when the ticket
    // completes). If queued, free the slot now.
    void DropSpec(const V2RoomSpec* s) {
        const int i = IndexOf(s);
        if (i < 0) return;
        if (slots[i].state == kInFlight) {
            slots[i].state = kAbandoned;
        } else {
            FreeSlot(i);
        }
    }
};

// streaming-experience Inc 3: is `cell` in the DRAWN 3×3 centered on `center`
// (Chebyshev distance ≤ 1 in the 2D XZ grid)? The drawn set is exactly the
// 3×3 — preloaded outer (cheb=2) cells are resident but never drawn. Host-safe.
inline bool IsDrawnCell(const V2RoomSpec& cell, const V2RoomSpec& center) {
    const int dx = cell.cell_ix - center.cell_ix;
    const int dz = cell.cell_iz - center.cell_iz;
    const int ax = dx < 0 ? -dx : dx;
    const int az = dz < 0 ? -dz : dz;
    const int cheb = ax > az ? ax : az;
    return cheb <= 1;
}

// Render-only near-pass tile streamer (Inc 3). Owns a bounded resident pool
// of `LvlRoomRenderer` instances (the near ring) and stream/evicts cells as
// the camera moves, never evicting the center. Gameplay stays active-only.
class TileStreamer {
public:
    TileStreamer() {
        // streaming-experience Inc 4: all residents start fully revealed.
        for (int i = 0; i < kMaxResident; ++i) reveal_scale_[i] = 1.0f;
    }
    ~TileStreamer();
    TileStreamer(const TileStreamer&) = delete;
    TileStreamer& operator=(const TileStreamer&) = delete;

    bool SetCenter(const MapSpecV2& spec, const V2RoomSpec& center,
                   const char* build_dir = nullptr);

    // Same as SetCenter but uses the camera direction to load a forward wedge
    // instead of a fixed 3×3 square. `camera_dir` is a normalized world-space
    // XZ forward vector; pass `{0,0,0}` to fall back to the square ring.
    bool SetCenter(const MapSpecV2& spec, const V2RoomSpec& center,
                   const Vec3& camera_dir, const char* build_dir = nullptr);

    // Set the material catalog used by the textured near pass (Inc 5). The
    // catalog is owned by the caller (GameplayScene) and must outlive the
    // streamer. Pass nullptr to force the flat-color fallback.
    void SetMaterialCatalog(const MaterialCatalog* catalog);

    // Attach the per-frame draw counters (Inc 1 / D7). Forwarded to every
    // resident room renderer; the orchestrator owns + resets them.
    void SetCounters(RenderCounters* counters);

    // Attach the per-phase profiler (Inc 1 / D7). The textured near pass wraps
    // each `TexturedRoomRenderer::Draw` in `kPhaseTextureUpload` so the TMEM
    // upload cost is measured separately from the high-priority pass total.
    void SetProfiler(n64::FrameProfiler* profiler);

    // streaming-experience Inc 1: attach the shared async staging buffer. The
    // streamer takes ownership and frees it in the destructor. `pool_size`
    // must be >= the largest cell's file size (Inc 2 sizes it from
    // `MapSpecV2::MaxLvlSize()` + margin). If never attached, the streamer
    // lazily allocates a `kStagingFallbackBytes` buffer on first use.
    void SetAsyncStagingPool(uint8_t* pool, uint32_t pool_size);

    // streaming-experience Inc 2: pre-reserve the streaming heap budget at
    // init. Allocates ONE contiguous `malloc_uncached(max_lvl_size +
    // kStagingMargin)` staging pool (no fragmentation) and attaches it via
    // `SetAsyncStagingPool`. Idempotent — only reserves once. Returns false on
    // allocation failure (the Inc 1 per-load fallback stays active).
    bool ReserveStreamingBudget(uint32_t max_lvl_size);

    // streaming-experience Inc 1: per-frame async-load pump. Polls the
    // in-flight DMA ticket, parses at most one completed cell per frame, and
    // issues the next queued cell when the staging slot is free. Called from
    // `OpenWorldRenderer::BeginFrame` (top of Update, before SetCenter issues
    // new loads — loads issued this frame are pumped next frame, acceptable
    // given the preload ring).
    void PumpAsyncLoads();

    // Per-frame resident visibility resolution. `cam` describes the near
    // camera so the AABB-cone test can be applied to decide which residents
    // are on-screen. Returns true if visibility was resolved. `dt` is the
    // frame delta in seconds (streaming-experience Inc 4: ages the reveal
    // animation); defaults to the locked 30 fps frame time.
    bool UpdateCamera(const CameraDesc& cam, float dt = 1.0f / 30.0f);

    void SetCameraPosition(const Vec3& camera_pos);

    void DrawLowPriority(const CameraDesc& cam);
    void DrawHighPriority(const CameraDesc& cam);

    int ResidentCount() const { return set_.count; }
    // streaming-experience Inc 5: how many cells are in the pending queue
    // (queued or in-flight). Telemetry / regression: with the backlog re-
    // enqueue enabled this drains toward 0 as the pump finishes the ring.
    int EnqueuedCount() const { return pending_.count; }
    int EvictedThisFrame() const { return evicted_this_frame_; }
    const ResidentSet& Set() const { return set_; }
    const ResidentVisibility& Visibility() const { return visibility_; }

    // streaming-experience Inc 4: the reveal scale for resident `i` (1.0 =
    // fully revealed). The streamer animates this onto cells that became
    // visible while still loading; `DrawHighPriority` passes it into the
    // renderer's `Draw`/`DrawMaterialRun`.
    float RevealScale(int i) const {
        if (i < 0 || i >= kMaxResident) return 1.0f;
        return reveal_scale_[i];
    }

private:
    // streaming-experience Inc 1: fallback staging size when no pool is
    // attached (>= the current manifest's largest cell, 93 KB, + margin).
    static constexpr uint32_t kStagingFallbackBytes = 120 * 1024;
    // streaming-experience Inc 2: margin added to `MaxLvlSize()` when sizing
    // the pre-reserved staging pool (headroom for the even-rounded DMA read).
    static constexpr uint32_t kStagingMargin = 32 * 1024;
    // streaming-experience Inc 2: the streaming heap budget, set from the
    // host measurement (`tests/streaming_memory_measure.cpp`): staging
    // `MaxLvlSize()` + margin (~125 KB on the current manifest) + headroom for
    // 25 resident renderers at the MEASURED max per-cell resident cost
    // (~70 KB for the 93 KB cell_n01_n02; mean ~6.9 KB) + allocator slack.
    // Worst-case 25-cell ring ≈ 1.75 MB; the heap grows via sbrk (no project
    // cap — the n64.ld ASSERT bounds the ROM image only), so this is a
    // planning/verification figure, not a hard allocation.
    static constexpr uint32_t kStreamingBudgetBytes = 2 * 1024 * 1024;

    // streaming-experience Inc 1: issue an async read for `spec` into the
    // staging buffer. Returns the DMA ticket (non-zero) or 0 on failure
    // (caller falls back to a synchronous Load). Device-only.
    uint64_t IssueAsyncRead(const V2RoomSpec* spec);

    // streaming-experience Inc 1: parse the staged bytes for `spec` into a
    // fresh TexturedRoomRenderer and place it into the resident slot. Returns
    // true on success. Device-only.
    bool PromoteStaged(const V2RoomSpec* spec);

    // streaming-experience Inc 1: synchronous fallback for `spec` (center at
    // boot, async-pool exhaustion, DMA issue failure, queue full, timeout).
    // Returns true if the cell is resident with a renderer afterwards.
    bool LoadSync(const V2RoomSpec& spec, const char* build_dir);

    // streaming-experience Inc 1: drop any pending-load record for `spec`
    // (called on eviction / free_all).
    void DropPending(const V2RoomSpec* spec) { pending_.DropSpec(spec); }

    // streaming-experience Inc 5: re-offer resident-but-not-ready-and-not-
    // tracked cells to the pending queue (backlog re-enqueue). Idempotent
    // (Enqueue dedups against tracked specs); no-ops when `async_ok` would be
    // false. Called once per pump tick.
    void RequeueResidentLoads();

    ResidentSet set_;
    ResidentVisibility visibility_;
    LvlRoomRenderer* renderers_[kMaxResident] = {};
    TexturedRoomRenderer* textured_renderers_[kMaxResident] = {};
    const MaterialCatalog* catalog_ = nullptr;
    RenderCounters* counters_ = nullptr;      // per-frame draw counters (Inc 1 / D7)
    n64::FrameProfiler* profiler_ = nullptr;    // per-phase profiler (Inc 1 / D7)
    int evicted_this_frame_ = 0;

    // streaming-experience Inc 1: async-load state.
    PendingLoadQueue pending_;          // bounded pending-load queue
    uint8_t* staging_ = nullptr;        // shared uncached staging buffer (owned)
    uint32_t staging_size_ = 0;
    uint32_t pump_frame_ = 0;           // monotonically increasing frame counter

    // streaming-experience Inc 4: per-resident reveal state. `reveal_scale_[i]`
    // is 1.0 when fully revealed; `revealing_[i]` is true while a cell that
    // became visible while still loading animates in (epsilon → 1 over
    // `kRevealDurationSec`). Reset to revealed on eviction/compaction and when
    // a cell leaves the drawn set.
    float reveal_scale_[kMaxResident] = {};
    bool revealing_[kMaxResident] = {};

    // streaming-experience Inc 4: reset a resident slot's reveal state to
    // fully-revealed (called on eviction/compaction and when a cell leaves the
    // drawn set).
    void ResetReveal(int i) {
        if (i >= 0 && i < kMaxResident) {
            reveal_scale_[i] = 1.0f;
            revealing_[i] = false;
        }
    }

    // Shared implementation of SetCenter with an optional camera direction.
    bool SetCenterImpl(const MapSpecV2& spec, const V2RoomSpec& center,
                       const Vec3& camera_dir, const char* build_dir);
};

}  // namespace madeline_cube
