#!/usr/bin/env python3
"""Inc 1 camera-relative int16 contract (host-side).

Asserts the camera-relative render foundation cannot overflow int16.

After Inc 1, the near pass expresses the world relative to the camera:
    packed = (world - camera) * kPosScale
`kPosScale = 32`, so the camera-relative delta must stay within ±1024 world
units on every axis (`|delta * 32| <= 32767`).

This test bakes the real 1.map, reads the bake's `chunk_size`/`scale` and cell
centers (render_origin) from the map-pack manifest, and sweeps a set of camera
positions along a representative player travel path. For every cell it asserts
the worst-case camera-relative delta still fits int16.

It mirrors the runtime camera-relative math (ToCameraSpace/PackedFitsInt16 in
src/user/gameplay/render/camera_space_math.hpp) and the bake's cell centers
(render_origin = cell center, tools/bake_interconnected_map.py).

Run:
    python3 tests/render_pipeline_contract.py
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

# kPosScale in the renderer (must match lvl_room_renderer.hpp).
K_POS_SCALE = 32.0
# Max int16 / kPosScale = 32767/32 ≈ 1023.97 world units.
MAX_WORLD = 32767.0 / K_POS_SCALE


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


def packed_fits_int16(world, camera, k_pos_scale):
    """Mirror of camera_space_math.hpp PackedFitsInt16."""
    limit = 32767.0
    dx = (world[0] - camera[0]) * k_pos_scale
    dy = (world[1] - camera[1]) * k_pos_scale
    dz = (world[2] - camera[2]) * k_pos_scale
    return abs(dx) <= limit and abs(dy) <= limit and abs(dz) <= limit


def test_camera_relative_does_not_overflow_int16():
    """For every cell, a camera anywhere in that cell (or its neighbors) keeps
    the cell's camera-relative geometry within int16 at kPosScale=32.

    The near pass only draws the active cell + its neighbors, so the camera is
    always close to the cells it renders. Each cell's geometry is within ±half
    a cell-width of its render_origin (cell center), so the worst-case delta is
    the camera at one cell corner and the far neighbor's geometry at the
    opposite corner — a couple of cell widths, comfortably under the ±1024-unit
    int16 limit. Distant cells beyond the near ring are the distant pass's job
    (Inc 4) with its own smaller kLodScale.
    """
    d = tempfile.mkdtemp(prefix="inc1-camera-")
    try:
        bake(d)
        from mappack_format import read_mappack_v2_binary
        pack = read_mappack_v2_binary(
            str(Path(d) / "staging" / "forsyken-city.mappack"))
        assert len(pack.rooms) > 0, "no rooms in bake"

        cell_w = pack.chunk_size * pack.scale
        half = cell_w / 2.0
        # Worst-case near-pass delta: camera at a cell corner, far-neighbor
        # geometry at the opposite corner of its cell. That is (2 cells - half)
        # in X and Z plus a Y bound. Assert half-cell plus a neighbor width
        # stays under MAX_WORLD.
        assert (half + cell_w) < MAX_WORLD, (
            f"half-cell+neighbor {half + cell_w:.0f} exceeds MAX_WORLD "
            f"{MAX_WORLD:.0f}; near-pass packing could overflow"
        )

        # Sweep camera positions: the center and four corners of every cell.
        # The near pass only renders the ACTIVE cell + its ±1 neighbors, so a
        # cell's geometry is only drawn when the camera is in that cell or an
        # adjacent one. Assert each cell's geometry fits from cameras located
        # at its own center/corners AND its 4 neighbors' centers — this is the
        # near-ring envelope. (Distant cells are the distant pass's job, Inc 4.)
        checked = 0
        center_cams = {}  # cell_id -> list of camera positions in that cell
        for r in pack.rooms:
            ox, oz = r.render_origin[0], r.render_origin[2]
            cams = [
                (ox, 50.0, oz),
                (ox - half, 50.0, oz - half),
                (ox + half, 50.0, oz - half),
                (ox - half, 50.0, oz + half),
                (ox + half, 50.0, oz + half),
            ]
            center_cams[r.id] = cams

        # Build neighbor lookups by cell_ix/cell_iz for near-ring enumeration.
        # Derive grid indices from render_origin (cell center =
        # (ix+0.5)*cell_w, (iz+0.5)*cell_w), matching bake_interconnected_map.
        def cell_index(wx, wz):
            return (int(math.floor(wx / cell_w)), int(math.floor(wz / cell_w)))

        by_cell = {}
        for r in pack.rooms:
            ix, iz = cell_index(r.render_origin[0], r.render_origin[2])
            by_cell[(ix, iz)] = r
        neighbor_dirs = [(-1, 0), (1, 0), (0, -1), (0, 1)]

        for r in pack.rooms:
            ix, iz = cell_index(r.render_origin[0], r.render_origin[2])
            ox, oz = r.render_origin[0], r.render_origin[2]
            # Worst-case world position for this cell: its four corners (its
            # geometry is within ±half a cell of the center).
            corners = [
                (ox - half, 0.0, oz - half),
                (ox + half, 0.0, oz + half),
                (ox - half, 0.0, oz + half),
                (ox + half, 0.0, oz - half),
            ]
            # Cameras that can legitimately render this cell: its own cell
            # center/corners plus each neighbor cell's center/corners.
            nearby = list(center_cams[r.id])
            for dx, dz in neighbor_dirs:
                nb = by_cell.get((ix + dx, iz + dz))
                if nb is not None:
                    nearby.extend(center_cams[nb.id])
            for cam in nearby:
                for w in corners:
                    assert packed_fits_int16(w, cam, K_POS_SCALE), (
                        f"{r.id} geometry {w} from near-ring camera {cam} "
                        f"overflows int16 at kPosScale {K_POS_SCALE}"
                    )
                    checked += 1
        print(
            f"PASS: {len(pack.rooms)} cells x near-ring cameras x 4 corners "
            f"({checked} checks) fit int16 in the near pass"
        )
    finally:
        shutil.rmtree(d, ignore_errors=True)


if __name__ == "__main__":
    test_camera_relative_does_not_overflow_int16()
    print("render_pipeline_contract: all checks passed")
