#!/usr/bin/env python3
"""Source-wiring contract for the disabled, unqualified horizon split.

This keeps known-risk DLOD code gated off in the stable profile while retaining
ownership guards for later device qualification. It does not replace ROM/Ares
visual validation.
"""

from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def ordered(source: str, *needles: str) -> None:
    cursor = -1
    for needle in needles:
        cursor = source.find(needle, cursor + 1)
        require(cursor >= 0, f"missing/out-of-order source marker: {needle}")


flags = (ROOT / "src/user/gameplay/debug_flags.hpp").read_text()
require("kEnableDistantTier = false" in flags,
        "stable profile must disable the unqualified z-split")

scene = (ROOT / "src/user/gameplay/scene/gameplay_scene.cpp").read_text()
require("if (kEnableDistantTier) {\n            open_world_.LoadDistantTier" in scene,
        "disabled z-split must not load DLOD map assets")
require(scene.count("t3d_viewport_create_buffered(3)") >= 3,
        "sky, distant, and near viewports each need buffered matrix storage")

renderer = (ROOT / "src/user/gameplay/render/open_world_renderer.cpp").read_text()
require("if (kEnableDistantTier && distant_tier_ && distant_viewport_)" in renderer,
        "disabled z-split must not submit DLOD draw calls")
ordered(
    renderer,
    "skybox_->Draw(near_cam);",
    "t3d_viewport_attach(static_cast<T3DViewport*>(distant_viewport_));",
    "distant_tier_->Draw(distant_cam, &counters_, *tile_streamer_);",
    "t3d_viewport_attach(static_cast<T3DViewport*>(near_viewport_));",
    "t3d_screen_clear_depth();",
    "RenderHighPriority(near_cam);",
)

distant = (ROOT / "src/user/gameplay/render/distant_tier_renderer.cpp").read_text()
require("near_streamer.IsCellDrawn(cell.cell_ix, cell.cell_iz)" in distant,
        "DLOD may yield only to a detailed cell actually drawn this frame")

streamer_header = (ROOT / "src/user/gameplay/render/tile_streamer.hpp").read_text()
streamer = (ROOT / "src/user/gameplay/render/tile_streamer.cpp").read_text()
require("ResolveResidentFrustumMask(visibility_, set_, cam);" in streamer,
        "near pass must cull with the current camera's 3D AABB frustum")
require("CellAabbInFrustum3D(" in streamer_header and "kCullMargin" in streamer_header,
        "near cells must use the shared conservative 3D frustum margin")
require("CellAabbInFrustum3D(" in distant and "kCullMargin" in distant,
        "near and DLOD must share culling predicate and margin")

telemetry = (ROOT / "src/user/rom_main.cpp").read_text()
require('"[render-phases] distant=' in telemetry,
        "telemetry must expose DLOD phase cost")
require('"[counters] distant_cells=%u distant_faces=%u ' in telemetry,
        "telemetry must expose visible DLOD work")

counter_header = (ROOT / "src/user/gameplay/render/open_world_renderer.hpp").read_text()
lvl_header = (ROOT / "src/user/gameplay/render/lvl_room_renderer.hpp").read_text()
for field, accessor in (
    ("distant_batches", "DrawBatchCount"),
    ("distant_vert_loads", "DrawVertLoadCount"),
    ("distant_syncs", "DrawSyncCount"),
):
    require(field in counter_header,
            f"RenderCounters must separate near and DLOD {field}")
    require(accessor in lvl_header,
            f"LvlRoomRenderer must expose exact precomputed DLOD {accessor}")
    require(f"counters->{field} +=" in distant,
            f"DLOD draw path must accumulate {field}")
    require(f"{field}=%u" in telemetry,
            f"ROM report must print {field}")

print("z_split_runtime_contract: all source-wiring checks passed")
