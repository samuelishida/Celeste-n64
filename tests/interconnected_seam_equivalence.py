#!/usr/bin/env python3
"""Inc 4 seam-equivalence test (host-side).

Asserts the bake's cell-resolution arithmetic matches the runtime's
`MapRuntime::ResolveCellByPosition` formula exactly, including values exactly
on seams and negative world coordinates.

The runtime formula (src/user/gameplay/world/map_runtime.cpp) is:
    cell = chunk_size * scale
    ix   = floor(world_x / cell)
    iz   = floor(world_z / cell)

The bake's `world_cell` (tools/ogworld/chunking.py) uses the same formula.
Because a Python test cannot call the C++ `ResolveCellByPosition`, it reads
the baked manifest's actual `chunk_size`/`scale` (not hardcoded 1200/0.2) and
asserts `world_cell` matches the runtime formula at seam and negative
coordinates using those manifest values.

COUPLING NOTE: this test independently re-derives the cell-resolution formula
from the manifest. If the C++ `ResolveCellByPosition` formula changes, update
BOTH this test AND the cross-reference comment in map_runtime.cpp.

Run:
    python3 tests/interconnected_seam_equivalence.py
"""

import sys
import math
import tempfile
import subprocess
import shutil
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tools"))

MAIN_MAP = REPO / "assets" / "og_converted" / "maps" / "1.map"
CHUNK_SIZE = 1200.0
SCALE = 0.2


def bake(out_dir: str) -> None:
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


def runtime_cell(world_x: float, world_z: float, cell: float):
    """Mirror of MapRuntime::ResolveCellByPosition's index arithmetic."""
    return (int(math.floor(world_x / cell)), int(math.floor(world_z / cell)))


def test_seam_equivalence():
    """Bake's world_cell matches the runtime formula at seams + negatives."""
    d = tempfile.mkdtemp(prefix="inc4-seam-")
    try:
        bake(d)
        from mappack_format import read_mappack_v2_binary
        pack = read_mappack_v2_binary(str(Path(d) / "staging" / "forsyken-city.mappack"))
        assert len(pack.rooms) > 0

        # Use the manifest's actual chunk_size/scale, not hardcoded values.
        cell = pack.chunk_size * pack.scale
        assert cell > 0.0

        from ogworld.chunking import resolve_cell_index, world_cell

        # Probe seam coordinates at cell boundaries and just off them, plus
        # negative coordinates. For each probe, the bake's world_cell must
        # equal the runtime formula's cell index. `world_cell` delegates to
        # the canonical `resolve_cell_index`, so this also asserts the
        # canonical Python helper matches the runtime formula.
        probes = []
        for n in range(-3, 4):
            seam = n * cell
            probes.append((seam, seam))            # exactly on a seam
            probes.append((seam + 1e-3, seam))     # just inside +X
            probes.append((seam - 1e-3, seam))     # just inside -X
            probes.append((seam, seam + 1e-3))     # just inside +Z
            probes.append((seam, seam - 1e-3))     # just inside -Z
        # Negative world coordinates (cells n01, n02, ...).
        probes.append((-cell * 0.5, -cell * 0.5))
        probes.append((-cell * 1.5, -cell * 1.5))
        probes.append((-cell * 2.5, -cell * 2.5))

        for (wx, wz) in probes:
            bake_cell = world_cell((wx, 0.0, wz), pack.chunk_size, pack.scale)
            canonical_cell = resolve_cell_index((wx, 0.0, wz), pack.chunk_size, pack.scale)
            runtime_cell_idx = runtime_cell(wx, wz, cell)
            assert bake_cell == canonical_cell, (
                f"world_cell {bake_cell} != canonical resolve_cell_index {canonical_cell}"
            )
            assert canonical_cell == runtime_cell_idx, (
                f"canonical {canonical_cell} != runtime {runtime_cell_idx}"
            )
            assert bake_cell == runtime_cell_idx, (
                f"world ({wx:.4f},{wz:.4f}): bake {bake_cell} != runtime "
                f"{runtime_cell_idx}"
            )
        print(f"PASS: bake world_cell == runtime formula at {len(probes)} "
              f"seam/negative probes (cell={cell:.1f})")
    finally:
        shutil.rmtree(d, ignore_errors=True)


def test_manifest_carries_chunk_size_scale():
    """The manifest actually stores chunk_size/scale (not hardcoded)."""
    d = tempfile.mkdtemp(prefix="inc4-manifest-")
    try:
        bake(d)
        from mappack_format import read_mappack_v2_binary
        pack = read_mappack_v2_binary(str(Path(d) / "staging" / "forsyken-city.mappack"))
        assert abs(pack.chunk_size - CHUNK_SIZE) < 1e-3, pack.chunk_size
        assert abs(pack.scale - SCALE) < 1e-6, pack.scale
        print(f"PASS: manifest carries chunk_size={pack.chunk_size} scale={pack.scale}")
    finally:
        shutil.rmtree(d, ignore_errors=True)


if __name__ == "__main__":
    test_seam_equivalence()
    test_manifest_carries_chunk_size_scale()
    print("ALL PASS")
