#pragma once

#include "gameplay/math_types.hpp"
#include "gameplay/world/mappack_loader.hpp"

namespace madeline_cube {

class LvlRoomRenderer;  // N64-only; forward-declared so this header is host-safe

// Pure neighbor-resolution helper (host-testable). Fills `out` with the
// center room plus its up-to-4 neighbors (in +X,-X,+Z,-Z order), resolved
// from `spec` by neighbor id. Returns the count (1..5). Missing neighbors
// (map edge or decoration-only cell) are skipped. `out` must have room for
// 5 entries. Inline so host tests can call it without linking the N64-only
// `ChunkRingRenderer.cpp`.
inline int ResolveRingRooms(const MapSpecV2& spec, const V2RoomSpec& center,
                            const V2RoomSpec* out[5]) {
    if (!out) return 0;
    int n = 0;
    out[n++] = &center;
    // Neighbor order: +X, -X, +Z, -Z (matches V2RoomSpec::neighbors layout).
    for (int axis = 0; axis < 4 && n < 5; ++axis) {
        const char* nid = center.neighbors[axis];
        if (nid[0] == '\0') continue;  // map edge or decoration-only cell
        const V2RoomSpec* nb = spec.FindRoom(nid);
        if (nb) out[n++] = nb;
    }
    return n;
}

// Render-only pool: loads the active cell + its four immediate ±X/±Z
// neighbors into `LvlRoomRenderer` instances and draws all of them each
// frame. This makes the interconnected world feel connected without
// affecting collision, actors, or respawn (gameplay stays active-only).
//
// N64-only: the .cpp includes libdragon/t3d and `LvlRoomRenderer`. On host
// the class is inert (Load returns false, Draw is a no-op) so the header can
// be included by host tests that exercise `ResolveRingRooms`.
class ChunkRingRenderer {
public:
    ChunkRingRenderer() = default;
    ~ChunkRingRenderer() { Free(); }

    ChunkRingRenderer(const ChunkRingRenderer&) = delete;
    ChunkRingRenderer& operator=(const ChunkRingRenderer&) = delete;

    // Load the center cell + its four neighbors, each rebased to its own
    // render origin. `build_dir` is null on device (loads rom:/lvl/...);
    // on host it localizes the rom:/ path to a filesystem path. A missing
    // neighbor LVL is skipped (not fatal); a missing center LVL is fatal.
    bool Load(const MapSpecV2& spec, const V2RoomSpec& center,
              const char* build_dir = nullptr);

    // Draw all loaded renderers. Call within a t3d_frame_start/end pair.
    void Draw() const;

    // Rebase every loaded renderer to draw camera-relative (see
    // `LvlRoomRenderer::SetCameraPosition`). Called once per frame before
    // `Draw()`. The near-pass view must be camera-at-origin to match.
    void SetCameraPosition(const Vec3& camera_pos);

    // Free all loaded renderers.
    void Free();

    // Number of renderers currently loaded (1..5).
    int LoadedCount() const { return loaded_count_; }

private:
    LvlRoomRenderer* renderers_[5] = {};
    int loaded_count_ = 0;
};

}  // namespace madeline_cube
