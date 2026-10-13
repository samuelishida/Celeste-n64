#!/usr/bin/env python3
"""Guard s10.5 UV interpolation for repeating world textures."""

from __future__ import annotations

import math
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

from ogworld.chunking import condition_texture_uvs, partition_world  # noqa: E402
from ogworld.geometry import build_world_geometry  # noqa: E402
from ogworld.parse import build_world_ir  # noqa: E402


def _assert_repeat_phase_preserved(before, after, period: float, axis: int) -> None:
    for old_uv, new_uv in zip(before, after):
        whole_tiles = (new_uv[axis] - old_uv[axis]) / period
        assert abs(whole_tiles - round(whole_tiles)) < 1e-6, (
            f"UV phase changed on repeated axis {axis}: {old_uv} -> {new_uv}"
        )


def _signed_overflow_crosses(values: list[float]) -> bool:
    bands = {math.floor((value * 32.0 + 32768.0) / 65536.0) for value in values}
    return len(bands) > 1


def main() -> int:
    repeat_profile = {
        "name": "world_repeat_rgba16",
        "output_width": 32,
        "output_height": 32,
        "wrap_s": "repeat",
        "wrap_t": "repeat",
    }
    before = ((1016.6, -1030.2), (1039.2, -1008.8), (1023.4, -1017.7))
    after = condition_texture_uvs(before, repeat_profile)
    _assert_repeat_phase_preserved(before, after, 32.0, 0)
    _assert_repeat_phase_preserved(before, after, 32.0, 1)
    assert all(-1024.0 <= uv[0] <= 32767 / 32 for uv in after)
    assert all(-1024.0 <= uv[1] <= 32767 / 32 for uv in after)
    assert not _signed_overflow_crosses([uv[0] for uv in after])
    assert not _signed_overflow_crosses([uv[1] for uv in after])

    mixed_profile = {
        "name": "world_repeat_rgba16",
        "output_width": 32,
        "output_height": 31,
        "wrap_s": "repeat",
        "wrap_t": "clamp",
    }
    mixed_before = ((3000.0, -1500.0), (3016.0, 1800.0), (2992.0, 900.0))
    mixed_after = condition_texture_uvs(mixed_before, mixed_profile)
    _assert_repeat_phase_preserved(mixed_before, mixed_after, 32.0, 0)
    assert all(-1024.0 <= uv[1] <= 32767 / 32 for uv in mixed_after)
    assert mixed_after[0][1] == -1024.0
    assert mixed_after[1][1] == 32767 / 32

    # Exercise actual clipped map polygons: all emitted texels must fit the
    # signed s10.5 coordinate range before the N64 runtime packs them.
    map_path = ROOT / "assets/og_converted/maps/1.map"
    build = build_world_ir(str(map_path), scale=0.2, strict=True)
    polygons, _ = build_world_geometry(build, scale=0.2)
    source_crossings = 0
    for polygon in polygons:
        profile = build.texture_profiles.get(polygon.texture, {})
        for axis, wrap_key in ((0, "wrap_s"), (1, "wrap_t")):
            if profile.get(wrap_key) == "repeat":
                source_crossings += int(_signed_overflow_crosses([uv[axis] for uv in polygon.uvs]))
    assert source_crossings > 0, "fixture no longer exercises signed UV overflow"

    chunks, diagnostics = partition_world(build, polygons, 1200.0, 0.2)
    assert not diagnostics, f"unexpected chunk diagnostics: {diagnostics[:3]}"
    checked_axes = 0
    for chunk in chunks.values():
        for polygon in chunk.polygons:
            profile = build.texture_profiles.get(polygon.texture, {})
            if profile.get("name") == "none":
                continue
            for axis in (0, 1):
                for value in (uv[axis] for uv in polygon.uvs):
                    packed = value * 32.0
                    assert -32768.0 <= packed <= 32767.0, (
                        f"cell {chunk.cell} face {polygon.entity_index}:"
                        f"{polygon.brush_index}:{polygon.face_index} UV axis {axis} "
                        f"overflows s10.5: {value} texels"
                    )
                    checked_axes += 1
    assert checked_axes > 0
    print(
        f"texture_uv_rebase_smoke: PASS ({source_crossings} source overflow axes, "
        f"{len(chunks)} cells, {checked_axes} packed UV values)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
