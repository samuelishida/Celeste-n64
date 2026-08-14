#pragma once

#include "gameplay/math_types.hpp"
#include "gameplay/render/dlod_format.hpp"

namespace madeline_cube {

class LvlRoomRenderer;  // N64-only; forward-declared so this header is host-safe

// Load a distant cell's geometry, preferring the compact `.dlod` (Inc 3 /
// compressed-LOD) and falling back to the `*_distant.lvl` when the `.dlod` is
// absent (rollback-safe until Inc 5 removes the LVL2 distant path).
//
// `pack_dir` is the map-pack id (e.g. "forsyken-city"); `chunk` is the cell
// id (e.g. "cell_00_00"). `render_origin` is the cell's world-space render
// origin; `pos_scale` must be `kLodScale` for the DLOD no-repack shortcut.
// `build_dir` is null on device (loads rom:/lvl/...); on host it localizes
// the path. `out` receives the loaded mesh. Returns true on success.
bool LoadDistantCellDlod(const char* pack_dir, const char* chunk,
                         const Vec3& render_origin, float pos_scale,
                         const char* build_dir, LvlRoomRenderer* out);

// Load all 4 directional meshes of a distant cell (Inc 4 / compressed-LOD).
// `out` is a 4-slot array; each slot is filled with a distinct mesh loaded
// from its direction section of the `.dlod`, or left null if that direction
// has no geometry. Falls back to loading a single mesh into `out[0]` (and
// sharing it across all slots) when the `.dlod` is absent or single-direction.
// Returns the number of non-null slots loaded (0 = no distant geometry).
int LoadDistantCellDlodAll(const char* pack_dir, const char* chunk,
                           const Vec3& render_origin, float pos_scale,
                           const char* build_dir, LvlRoomRenderer* out[4]);

}  // namespace madeline_cube
