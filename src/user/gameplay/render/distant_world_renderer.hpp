#pragma once

#include <cstdint>

#include "gameplay/math_types.hpp"
#include "gameplay/render/pass_camera_math.hpp"
#include "gameplay/world/mappack_loader.hpp"  // MapSpecV2, V2RoomSpec

namespace madeline_cube {

class LvlRoomRenderer;  // N64-only; forward-declared so this header is host-safe

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
    LvlRoomRenderer* meshes[kMaxDirMeshes] = {};
    DistantLodEntry* children[kMaxChildren] = {};
};

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

    // Rebase every distant mesh translation to draw camera-relative at the
    // compressed scale.
    void SetCameraPosition(const Vec3& camera_pos);

    // Render the distant pass: Z off, back-to-front by priority, Z on after.
    void Render(const CameraDesc& cam);

    void UpdateCamera(const Vec3& camera_pos, const CameraDesc& cam);

    // Number of loaded distant entries.
    int EntryCount() const { return entry_count_; }

    // Host-testable access to the LOD table (used by distant_pass_order.cpp).
    const DistantLodEntry* Entries() const { return entries_; }

    // The compressed coordinate scale used to pack distant vertices.
    static constexpr float kLodScale = 0.25f;

private:
    DistantLodEntry entries_[64];       // one per cell (kMaxRooms cap)
    int entry_count_ = 0;
    Vec3 camera_pos_ = {0.0f, 0.0f, 0.0f};
};

}  // namespace madeline_cube
