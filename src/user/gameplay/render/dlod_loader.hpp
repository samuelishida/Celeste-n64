#pragma once

#include "gameplay/math_types.hpp"
#include "gameplay/render/dlod_format.hpp"

namespace madeline_cube {

class LvlRoomRenderer;  // N64-only; forward-declared so this header is host-safe

// Load all 4 directional meshes of a distant cell (Inc 4 / compressed-LOD).
// `out` is a 4-slot array; each slot is filled with a distinct mesh loaded
// from its direction section of the `.dlod`, or left null if that direction
// has no geometry. Falls back to loading a single mesh into `out[0]` (slots
// 1..3 null — Inc 3 / D2 stop-sharing) when the `.dlod` is single-direction.
// `out_shared_origin` (optional) receives the DLOD header's SHARED map-center
// origin (Inc 3 / D2), the source of truth for packing — the caller's
// per-cell render origin is NOT used (the header origin supersedes it).
// `pos_scale` must be `kLodScale` for the DLOD no-repack shortcut. `build_dir`
// is null on device (loads rom:/lvl/...); on host it localizes the path.
// Returns the number of non-null slots loaded (0 = no distant geometry).
int LoadDistantCellDlodAll(const char* pack_dir, const char* chunk,
                           float pos_scale, const char* build_dir,
                           LvlRoomRenderer* out[4],
                           Vec3* out_shared_origin = nullptr);

}  // namespace madeline_cube
