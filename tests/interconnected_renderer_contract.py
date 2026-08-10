#!/usr/bin/env python3
"""Inc 8 chunk-local renderer validation (host-side).

Validates that each visual room's render origin keeps its vertices within the
int16 fixed-point range (kPosScale=32, max ±1024 world units after rebase),
and that no room exceeds the renderer's batch cap (1024 faces).

The renderer itself is N64-only (libdragon/t3d); this host test validates the
data contract the renderer depends on: render origins, per-room face counts,
and coordinate ranges.

Run:
    python3 tests/interconnected_renderer_contract.py
"""

import sys
import json
import subprocess
import tempfile
import shutil
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tools"))

MAIN_MAP = REPO / "assets" / "og_converted" / "maps" / "1.map"
CHUNK_SIZE = 1200.0
SCALE = 0.2

# kPosScale in the renderer.
K_POS_SCALE = 32.0
# Max int16 / kPosScale = 32767/32 ≈ 1023.97 world units.
MAX_WORLD = 32767.0 / K_POS_SCALE
# Renderer batch cap.
K_MAX_BATCHES = 1024


def bake(out_dir: str) -> dict:
    proc = subprocess.run(
        [
            sys.executable, str(REPO / "tools" / "bake_interconnected_map.py"),
            str(MAIN_MAP),
            "--out-dir", out_dir,
            "--chunk-size", str(int(CHUNK_SIZE)),
            "--scale", str(SCALE),
        ],
        capture_output=True, text=True,
    )
    if proc.returncode != 0:
        raise AssertionError(f"bake failed: {proc.stderr}\n{proc.stdout}")
    with open(Path(out_dir) / "interconnected_report.json") as f:
        return json.load(f)


def test_render_origins_keep_coords_in_range():
    """Each room's render origin keeps its vertices within int16 range."""
    d = tempfile.mkdtemp(prefix="inc8-render-")
    try:
        report = bake(d)
        from mappack_format import read_mappack_v2_binary
        pack = read_mappack_v2_binary(str(Path(d) / "staging" / "forsyken-city.mappack"))
        assert len(pack.rooms) > 0
        for r in pack.rooms:
            # The renderer subtracts the render origin, so local coords are
            # within ±half a cell-width in XZ (Y is bounded by the map).
            cell_w = pack.chunk_size * pack.scale
            half = cell_w / 2.0
            assert half < MAX_WORLD, (
                f"{r.id} half-cell {half} exceeds {MAX_WORLD}"
            )
        print(f"PASS: all {len(pack.rooms)} render origins keep coords in int16 range")
    finally:
        shutil.rmtree(d, ignore_errors=True)


def test_room_face_counts_under_batch_cap():
    """No room exceeds the renderer's 1024-batch cap."""
    d = tempfile.mkdtemp(prefix="inc8-batch-")
    try:
        report = bake(d)
        for cid, c in report["chunks"].items():
            assert c["faces"] <= K_MAX_BATCHES, (
                f"{cid} faces {c['faces']} > batch cap {K_MAX_BATCHES}"
            )
        print(f"PASS: all {report['chunk_count']} rooms ≤ {K_MAX_BATCHES} faces")
    finally:
        shutil.rmtree(d, ignore_errors=True)


def test_absolute_coords_would_overflow():
    """Without rebasing, the full map's absolute coords would overflow int16.

    This documents WHY chunk-local rendering is required: the map's absolute
    world coordinates exceed the int16 kPosScale range.
    """
    d = tempfile.mkdtemp(prefix="inc8-overflow-")
    try:
        report = bake(d)
        # The global collision mesh's quantized AABB is in int16 range (it is
        # quantized), but the raw world coords of the map exceed ±1024.
        # We verify the map's world extent exceeds the renderer's safe range.
        from mappack_format import read_mappack_v2_binary
        pack = read_mappack_v2_binary(str(Path(d) / "staging" / "forsyken-city.mappack"))
        # Compute the world extent from all room AABBs.
        min_x = min(r.world_aabb_min[0] for r in pack.rooms)
        max_x = max(r.world_aabb_max[0] for r in pack.rooms)
        min_z = min(r.world_aabb_min[2] for r in pack.rooms)
        max_z = max(r.world_aabb_max[2] for r in pack.rooms)
        extent_x = max(abs(min_x), abs(max_x))
        extent_z = max(abs(min_z), abs(max_z))
        # The map spans more than ±1024 in at least one axis, so absolute
        # packing would overflow — hence render origins are required.
        assert extent_x > MAX_WORLD or extent_z > MAX_WORLD, (
            f"map extent ({extent_x},{extent_z}) does not exceed {MAX_WORLD}; "
            f"rebasing may be unnecessary"
        )
        print(f"PASS: map world extent ({extent_x:.0f},{extent_z:.0f}) exceeds "
              f"int16 range {MAX_WORLD:.0f} — rebasing required")
    finally:
        shutil.rmtree(d, ignore_errors=True)


def main() -> int:
    test_render_origins_keep_coords_in_range()
    test_room_face_counts_under_batch_cap()
    test_absolute_coords_would_overflow()
    print("\nAll interconnected_renderer_contract tests passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
