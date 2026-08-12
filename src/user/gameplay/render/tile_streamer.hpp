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

// ── Resident-pool constants ──────────────────────────────────────────
// `kMaxRing` is the near-pass resident capacity: the center cell + its
// up-to-8 ring neighbors (Chebyshev distance 1 in the 2D XZ grid). This is
// the same `kMaxVisibleCells` budget the plan caps the near pass at.
constexpr int kMaxRing = 9;

// Resolve the near-pass resident ring: the center cell plus every manifest
// room within Chebyshev distance 1 on the 2D XZ grid (|dx|<=1 && |dz|<=1).
// The center is always `out[0]`; map-edge cells simply have fewer neighbors
// (never fatal). Returns the count (1..kMaxRing). `out` must have room for
// `out_capacity` entries. Host-safe — no N64 types.
inline int ResolveDistanceRing(const MapSpecV2& spec, const V2RoomSpec& center,
                               const V2RoomSpec* out[], int out_capacity) {
    if (!out || out_capacity <= 0) return 0;
    int n = 0;
    out[n++] = &center;
    for (int i = 0; i < spec.room_count && n < out_capacity; ++i) {
        const V2RoomSpec& r = spec.rooms[i];
        if (r.id[0] == '\0') continue;
        if (&r == &center) continue;
        const int dx = r.cell_ix - center.cell_ix;
        const int dz = r.cell_iz - center.cell_iz;
        if (dx < -1 || dx > 1 || dz < -1 || dz > 1) continue;
        out[n++] = &r;
    }
    return n;
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
    const V2RoomSpec* spec[kMaxRing] = {};
    uint32_t last_used[kMaxRing] = {};
    int count = 0;
    int capacity = kMaxRing;

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

    // Index of the least-recently-used resident that is NOT the center, or -1
    // if every resident is the center (nothing evictable).
    int EvictCandidate(const V2RoomSpec* center) const {
        int best = -1;
        uint32_t best_frame = UINT32_MAX;
        for (int i = 0; i < count; ++i) {
            if (spec[i] == center) continue;  // never evict the center
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

// Render-only near-pass tile streamer (Inc 3). Owns a bounded resident pool
// of `LvlRoomRenderer` instances (the near ring) and stream/evicts cells as
// the camera moves, never evicting the center. Gameplay stays active-only.
class TileStreamer {
public:
    TileStreamer() = default;
    ~TileStreamer();
    TileStreamer(const TileStreamer&) = delete;
    TileStreamer& operator=(const TileStreamer&) = delete;

    bool SetCenter(const MapSpecV2& spec, const V2RoomSpec& center,
                   const char* build_dir = nullptr);

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

    void UpdateCamera();

    void SetCameraPosition(const Vec3& camera_pos);

    void DrawLowPriority(const CameraDesc& cam);
    void DrawHighPriority(const CameraDesc& cam);

    int ResidentCount() const { return set_.count; }
    int EvictedThisFrame() const { return evicted_this_frame_; }
    const ResidentSet& Set() const { return set_; }

private:
    ResidentSet set_;
    LvlRoomRenderer* renderers_[kMaxRing] = {};
    TexturedRoomRenderer* textured_renderers_[kMaxRing] = {};
    const MaterialCatalog* catalog_ = nullptr;
    RenderCounters* counters_ = nullptr;      // per-frame draw counters (Inc 1 / D7)
    n64::FrameProfiler* profiler_ = nullptr;  // per-phase profiler (Inc 1 / D7)
    int evicted_this_frame_ = 0;
};

}  // namespace madeline_cube
