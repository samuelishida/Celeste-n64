#pragma once

#include <cstdint>

#include "gameplay/math_types.hpp"
#include "gameplay/render/fog_math.hpp"
#include "gameplay/render/lod_math.hpp"
#include "gameplay/render/pass_camera_math.hpp"
#include "gameplay/world/mappack_loader.hpp"  // MapSpecV2, V2RoomSpec

namespace n64 {
class FrameArena;  // defined in n64/frame_arena.hpp (Inc 5 / D6)
}  // namespace n64

namespace madeline_cube {

class LvlRoomRenderer;  // N64-only; forward-declared so this header is host-safe
struct RenderCounters;  // defined in open_world_renderer.hpp (Inc 1 / D7)

// LOD hierarchy entry for one distant cell (arch.md §9-11). A cell may hold
// up to 4 directional meshes (`meshes[4]`, indexed N/S/E/W per lod_math.hpp);
// in the first version these all point to the same loaded distant mesh, but
// the data shape supports per-direction variants. `children` reference finer
// LOD entries; `child_count` is how many. Host-safe (meshes are opaque
// pointers never dereferenced by the host-testable list builder).
struct DistantLodEntry {
    static constexpr int kMaxChildren = 8;
    static constexpr int kMaxDirMeshes = 4;

    int child_count = 0;
    float lod_scale = 0.25f;
    int priority = 0;
    int cell_ix = 0;
    int cell_iz = 0;
    Vec3 origin = {0.0f, 0.0f, 0.0f};  // cell center (world)
    // Inc 4 / D3: the cell's world XZ AABB (from the manifest). Used by the
    // extent-aware distant cull so a cell is culled only when its WHOLE AABB
    // is outside the cone (no screen-edge pop-in). Zero-extent → treated as
    // the cell center (today's behavior).
    AABB aabb = {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}};
    LvlRoomRenderer* meshes[kMaxDirMeshes] = {};
    DistantLodEntry* children[kMaxChildren] = {};
};

// Collect the DISTINCT non-null mesh pointers in an entry's directional slots
// into `out` (bounded by `kMaxDirMeshes`). Host-safe: only compares pointers,
// never dereferences them — so a host test can pass fake pointer values
// without instantiating the N64 `LvlRoomRenderer`. Returns the distinct
// pointer count. Used by `FreeEntries` so a shared-slot entry (all 4 slots
// pointing at the same mesh) frees each distinct mesh exactly once.
inline int CollectDistinctMeshes(const DistantLodEntry& en,
                                 LvlRoomRenderer* out[DistantLodEntry::kMaxDirMeshes]) {
    int n = 0;
    for (int d = 0; d < DistantLodEntry::kMaxDirMeshes; ++d) {
        LvlRoomRenderer* m = en.meshes[d];
        if (!m) continue;
        bool seen = false;
        for (int k = 0; k < n; ++k) {
            if (out[k] == m) { seen = true; break; }
        }
        if (!seen) out[n++] = m;
    }
    return n;
}

// One item in the distant render list (arch.md §8). `cell_index` indexes into
// the renderer's `entries_[]`; `priority` orders back-to-front (higher =
// drawn first = farther). Host-testable — no N64 types.
struct DistantRenderItem {
    int cell_index = -1;
    float distance = 0.0f;
    uint32_t priority = 0;
};

// Build the distant render list (host-testable): for every loaded LOD entry,
// assign a distance-derived priority (arch.md §8) so far cells draw first in
// the back-to-front distant pass. `camera_pos` is the world-space camera
// position used to compute each entry's distance. Returns the number of items
// (bounded by `out_capacity`).
inline int BuildDistantRenderList(const Vec3& camera_pos,
                                  const DistantLodEntry* entries,
                                  int entry_count,
                                  DistantRenderItem out[],
                                  int out_capacity) {
    if (!out || out_capacity <= 0 || !entries) return 0;
    int n = 0;
    for (int e = 0; e < entry_count && n < out_capacity; ++e) {
        const DistantLodEntry& en = entries[e];
        const float dx = camera_pos.x - en.origin.x;
        const float dz = camera_pos.z - en.origin.z;
        const float dist = dx * dx + dz * dz;
        DistantRenderItem item;
        item.cell_index = e;
        item.distance = dist;
        // arch.md §8: priority orders back-to-front; far cells (high dist)
        // draw first. Using distance² as the key is monotonic with distance.
        item.priority = static_cast<uint32_t>(dist);
        out[n++] = item;
    }
    return n;
}

// Build the distant render list keeping only cells inside the camera's
// horizontal view cone + depth range (Inc 2 / D2). Same ordering contract as
// `BuildDistantRenderList` (far cells draw first via priority = distance²),
// but entries failing `CellAabbInDistantFrustum` are omitted. `hfov_deg` is the
// horizontal full FOV in degrees (from vertical FOV + 4:3 aspect); `near_d` /
// `far_d` are the DISTANT pass clip range (not the near camera's, so cells
// behind the near far-plane are still drawn here). Host-testable — no N64
// types. `BuildDistantRenderList` (unculled) is intentionally untouched so the
// `distant_pass_order` sort contract is unchanged.
inline int BuildDistantRenderListCulled(const Vec3& camera_pos,
                                        const Vec3& camera_target,
                                        const DistantLodEntry* entries,
                                        int entry_count,
                                        DistantRenderItem out[],
                                        int out_capacity,
                                        float hfov_deg, float near_d,
                                        float far_d, float margin = 1.15f,
                                        float max_dist2 = 0.0f) {
    if (!out || out_capacity <= 0 || !entries) return 0;
    int n = 0;
    for (int e = 0; e < entry_count && n < out_capacity; ++e) {
        const DistantLodEntry& en = entries[e];
        // Inc 4 / D3: extent-aware cull — a cell is culled only when its WHOLE
        // XZ AABB is outside the cone + depth range. A zero-extent AABB falls
        // back to the cell center (today's behavior).
        AABB aabb = en.aabb;
        if (aabb.min.x == aabb.max.x && aabb.min.z == aabb.max.z) {
            // Degenerate AABB → treat as the cell center (en.origin).
            aabb.min = en.origin;
            aabb.max = en.origin;
        }
        if (!CellAabbInDistantFrustum(camera_pos, camera_target, hfov_deg,
                                      near_d, far_d, aabb, margin)) {
            continue;
        }
        // Inc 2 / D1: skip cells beyond the squared-distance threshold so the
        // horizon fades into fog instead of drawing the whole map. `max_dist2
        // <= 0` means no limit (preserves the existing cull contract).
        if (!CellWithinDistance(camera_pos, en.origin, max_dist2)) {
            continue;
        }
        const float dx = camera_pos.x - en.origin.x;
        const float dz = camera_pos.z - en.origin.z;
        const float dist = dx * dx + dz * dz;
        DistantRenderItem item;
        item.cell_index = e;
        item.distance = dist;
        item.priority = static_cast<uint32_t>(dist);
        out[n++] = item;
    }
    return n;
}

// Compressed-coordinate distant-world renderer (Inc 4). Renders the coarse
// distant representation of the world so the horizon is visible, using a
// separate camera with compressed coordinates and no Z-buffer (back-to-front
// sorted). Loads one coarse distant LVL per cell into a `LvlRoomRenderer`
// packed at `kLodScale` (the compressed coordinate scale).
class DistantWorldRenderer {
public:
    DistantWorldRenderer() = default;
    ~DistantWorldRenderer();
    DistantWorldRenderer(const DistantWorldRenderer&) = delete;
    DistantWorldRenderer& operator=(const DistantWorldRenderer&) = delete;

    // Load coarse distant meshes for every cell in `spec`. `build_dir` is
    // null on device (loads rom:/lvl/...); on host it localizes the path.
    // Builds the `entries_` LOD table (one entry per cell). A cell with no
    // distant mesh (no renderable geometry) is skipped (non-fatal).
    bool Load(const MapSpecV2& spec, const char* build_dir = nullptr);

    // Inc 6 / D5: stream the distant tier by camera cell. Resident = cells
    // within `kDistantStreamRadius` (Chebyshev) of `center`; cells outside are
    // evicted (all direction meshes freed via the Inc 2 dedupe), missing
    // in-radius cells are loaded. `entry_count_` = resident count. Runs in
    // `SetCenter` (Update phase, before the frame's Render builds its draw
    // list) — no eviction may occur between list-build and draw (use-after-free
    // guard). Returns true if any cells are resident.
    bool StreamToCenter(const MapSpecV2& spec, const V2RoomSpec& center,
                        const char* build_dir = nullptr);

    // Rebase every distant mesh translation to draw camera-relative at the
    // compressed scale.
    void SetCameraPosition(const Vec3& camera_pos);

    // Set the fog applied to the distant pass (Inc 6). Fog is configured
    // before the distant cells and torn down before the near pass.
    void SetFog(const FogParams& fog);

    // Render the distant pass: Z off, back-to-front by priority, Z on after.
    void Render(const CameraDesc& cam);

    void UpdateCamera(const Vec3& camera_pos, const CameraDesc& cam);

    // Number of loaded distant entries.
    int EntryCount() const { return entry_count_; }

    // Free every loaded distant mesh (all distinct directional slots) and reset
    // the entry table. Idempotent. Called by the destructor and the reload
    // path in Load().
    void FreeEntries();

    // Attach the per-frame draw counters (Inc 1 / D7). The renderer increments
    // `distant_cells` per cell drawn; the orchestrator owns + resets them.
    void SetCounters(RenderCounters* counters) { counters_ = counters; }

    // Attach the frame-scoped arena (Inc 5 / D6). The per-frame distant render
    // list is allocated from it (64 KB is ample for ≤ 64 items); a null/empty
    // arena falls back to a small stack buffer.
    void SetArena(n64::FrameArena* arena) { arena_ = arena; }

    // Inc 5 / D4: set this frame's near-draw cell indices (the exact cells the
    // near pass will draw). The distant pass skips these so no cell is drawn
    // by both passes (overlap handoff). Computed once per frame by the
    // orchestrator (OpenWorldRenderer::Render) and passed here. `count` ≤
    // kNearDrawSetCap. A null/empty set → no skip (draw everything as today).
    void SetNearDrawSet(const int ix[], const int iz[], int count) {
        near_count_ = count > kNearDrawSetCap ? kNearDrawSetCap : count;
        for (int i = 0; i < near_count_; ++i) {
            near_ix_[i] = ix[i];
            near_iz_[i] = iz[i];
        }
    }

    // Host-testable access to the LOD table (used by distant_pass_order.cpp).
    const DistantLodEntry* Entries() const { return entries_; }

    // Per-cell cost summary captured each frame during the distant draw
    // (Inc 3 / instrumentation). `distance_sq` is dx²+dz² (matches
    // `DistantRenderItem.distance`), NOT euclidean distance. `runs` is the
    // active-path draw unit count (runs when IsActiveRunPath, else per-face
    // batches) — the true RSP sync driver. `verts` is the baked cell vertex
    // count, a cell-size proxy (NOT the per-frame run span ≤70 loaded into
    // DMEM). Host-safe — plain fields.
    struct DistantCellStat {
        int cell_ix = 0;
        int cell_iz = 0;
        int runs = 0;
        int verts = 0;
        float distance_sq = 0.0f;
    };

    // Number of drawn cells captured this frame (≤ kDistantCellStatCap).
    int CellStatCount() const { return cell_stat_count_; }

    // The per-frame captured cell stats (valid for CellStatCount() entries).
    const DistantCellStat* CellStats() const { return cell_stats_; }

    // The compressed coordinate scale used to pack distant vertices.
    static constexpr float kLodScale = 0.25f;

    // Inc 6 / D5: the Chebyshev stream radius (in cells) for the distant tier.
    // Resident = cells within this radius of the camera cell. Derived so the
    // worst-case load distance (radius × cell − half-cell, because distance
    // tests hit the cell center) stays ≥ the fog-complete distance:
    //   6·240 − 120 = 1320u > 1197u (fog completes at 0.9·sqrt(kDistantMaxDist2))
    // so a cell is always fully fogged before it can become drawable — eviction
    // / load is invisible. INVARIANT: radius ≥ ceil(fog_complete/cell + 0.5).
    static constexpr int kDistantStreamRadius = 6;

private:
    // Inc 6 / D5: load one cell's distant meshes into `entries_[entry_count_]`
    // (incrementing it on success). Returns true if the cell loaded. Shared by
    // `Load` and `StreamToCenter`.
    bool LoadCell(const V2RoomSpec& rs, const char* build_dir);
    // Cap for the per-cell cost array (Inc 3 / instrumentation). Matches the
    // `entries_` cap (64): you cannot draw more cells than you have entries.
    static constexpr int kDistantCellStatCap = 64;

    DistantLodEntry entries_[64];       // one per cell (kMaxRooms cap)
    int entry_count_ = 0;
    Vec3 camera_pos_ = {0.0f, 0.0f, 0.0f};
    // Inc 3 / D2: the SHARED map-center origin all distant verts pack against
    // (from the DLOD header). The whole distant pass draws under ONE
    // camera-relative matrix built from this origin. Unset (all zeros) until
    // the first cell loads; Render no-ops if no cells are loaded.
    Vec3 shared_origin_ = {0.0f, 0.0f, 0.0f};
    // Inc 3 / D2: the shared pass matrix, allocated in UNCACHED memory (the
    // RSP DMAs it at command-execution time, so a cached stack local would be
    // read stale). Allocated lazily in Render, freed in the destructor.
    // Stored as void* so this header stays host-safe (T3DMat4FP is a t3d
    // typedef; the .cpp casts to the real type).
    void* shared_matrix_fp_ = nullptr;
    FogParams fog_;
    RenderCounters* counters_ = nullptr;  // per-frame draw counters (Inc 1 / D7)
    n64::FrameArena* arena_ = nullptr;    // frame-scoped arena (Inc 5 / D6)
    // Per-cell cost summary captured during Render (Inc 3 / instrumentation).
    // Plain member array — no allocation, no arena dependency; `cell_stat_count_`
    // is reset to 0 at the top of Render so stale data never survives.
    DistantCellStat cell_stats_[kDistantCellStatCap];
    int cell_stat_count_ = 0;
    // Inc 5 / D4: this frame's near-draw cell indices (the exact cells the near
    // pass will draw). The distant pass skips these so no cell is drawn by
    // both passes. Bounded by the near resident pool (center + Chebyshev-1
    // ring = 9 cells).
    static constexpr int kNearDrawSetCap = 9;
    int near_ix_[kNearDrawSetCap] = {};
    int near_iz_[kNearDrawSetCap] = {};
    int near_count_ = 0;
};

}  // namespace madeline_cube
