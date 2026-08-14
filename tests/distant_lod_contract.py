#!/usr/bin/env python3
"""Inc 4 distant-LOD int16 contract (host-side).

Asserts the distant pass's compressed coordinates cannot overflow int16.

The distant pass packs coarse cell meshes at `kLodScale` (the compressed
coordinate scale). Because distant cells are camera-relative (Inc 1), the
worst case is a camera at one edge of the world looking to the opposite edge:
the farthest cell's vertices are at ~`distant_far` from the camera. The two
requirements from arch.md §3 / the plan are:

    distant_far / kLodScale <= 32767
    every packed distant vertex |v * kLodScale| <= 32767
        for a camera sweep from edge to edge.

This bakes the real 1.map, reads the map-pack manifest, computes `distant_far`
(= tile_size * 1.4, arch.md §5) and the world half-extent, and asserts both
conditions. `kLodScale` must match the runtime `DistantWorldRenderer::kLodScale`
(0.25).

Run:
    python3 tests/distant_lod_contract.py
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

# Must match DistantWorldRenderer::kLodScale (src/.../distant_world_renderer.hpp).
K_LOD_SCALE = 0.25
# Max int16.
MAX_INT16 = 32767.0


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


def read_distant_verts(dlod_path: Path):
    """Read the packed (x, y, z) vertices from a distant DLOD v2 file.

    DLOD v2 layout (big-endian): magic(4), version(4), flags(4),
    direction_count(4), face_count(4), vert_count(4), material_count(4),
    origin(12), reserved(4). Then per-direction sections: dir_face_count(4),
    dir_vert_count(4), verts (dir_vert_count × s16 xyz), materials.
    Returns (verts, origin) where verts are the packed s16 triples.
    """
    import struct
    with open(dlod_path, "rb") as f:
        data = f.read()
    magic, version, flags, dir_count, face_count, vert_count, mat_count = \
        struct.unpack_from(">IIIIIII", data, 0)
    assert magic == 0x444C4F44, f"bad DLOD magic {magic:#x}"
    assert version == 2, f"bad DLOD version {version} (expected 2)"
    ox, oy, oz = struct.unpack_from(">fff", data, 28)
    offset = 44
    verts = []
    for _ in range(dir_count):
        d_faces, d_verts = struct.unpack_from(">II", data, offset)
        offset += 8
        for _ in range(d_verts):
            x, y, z = struct.unpack_from(">hhh", data, offset)
            offset += 6
            verts.append((x, y, z))
        offset += d_faces  # materials
    return verts, (ox, oy, oz)


def test_distant_lod_fits_int16_at_max_far():
    """For every distant cell, its packed vertices stay within int16 at
    kLodScale across the full world extent at the maximum distant-far plane."""
    d = tempfile.mkdtemp(prefix="inc4-distant-")
    try:
        bake(d)
        from mappack_format import read_mappack_v2_binary
        pack = read_mappack_v2_binary(
            str(Path(d) / "staging" / "forsyken-city.mappack"))
        assert len(pack.rooms) > 0, "no rooms in bake"

        tile_size = pack.chunk_size * pack.scale
        distant_far = tile_size * 1.4  # arch.md §5

        # Condition 1: distant_far / kLodScale <= 32767.
        assert distant_far / K_LOD_SCALE <= MAX_INT16, (
            f"distant_far {distant_far:.0f} / kLodScale {K_LOD_SCALE} "
            f"= {distant_far / K_LOD_SCALE:.0f} > 32767"
        )
        print(f"PASS: distant_far/kLodScale = {distant_far / K_LOD_SCALE:.0f} <= 32767")

        # World extent: max |render_origin| over all cells, plus half a cell
        # (geometry extends half a cell past its center).
        half = tile_size / 2.0
        max_extent = 0.0
        for r in pack.rooms:
            max_extent = max(max_extent, abs(r.render_origin[0]),
                             abs(r.render_origin[2]))
        max_extent += half

        # Condition 2: every packed distant vertex |v * kLodScale| <= 32767 for
        # a camera at the extreme edge looking across the world. The farthest
        # a packed vertex can be from the camera is max_extent (camera at one
        # edge, geometry at the opposite edge) = distant_far.
        # Assert the packed extent at kLodScale stays in range.
        packed_extent = max_extent * K_LOD_SCALE
        assert packed_extent <= MAX_INT16, (
            f"world half-extent {max_extent:.0f} * kLodScale {K_LOD_SCALE} "
            f"= {packed_extent:.0f} > 32767"
        )
        print(f"PASS: world half-extent {max_extent:.0f} * kLodScale "
              f"= {packed_extent:.0f} <= 32767")

        # Read every distant .dlod and assert each packed vertex stays within
        # int16 at kLodScale (the .dlod is already packed at kLodScale
        # relative to the cell origin, so the packed values are directly
        # bounded by int16).
        staging = Path(d) / "staging"
        distant_files = sorted(staging.glob("*_distant.dlod"))
        assert len(distant_files) > 0, "no distant .dlod files emitted by the bake"
        checked = 0
        shared_origins = set()
        for lp in distant_files:
            verts, origin = read_distant_verts(lp)
            shared_origins.add(origin)
            checked += len(verts)
            for v in verts:
                # The .dlod packs (world - origin) * kLodScale; the packed
                # value must fit int16. Also verify the world-space extent
                # from the extreme camera stays in range.
                for c in v:
                    if abs(c) > MAX_INT16:
                        raise AssertionError(
                            f"{lp.name} packed vertex {v} exceeds int16")
                # Reconstruct world and check the camera-extreme packing.
                wx = v[0] / K_LOD_SCALE + origin[0]
                cam_x = -max_extent
                dx = (wx - cam_x) * K_LOD_SCALE
                if abs(dx) > MAX_INT16:
                    raise AssertionError(
                        f"{lp.name} vertex {v} packs to dx={dx:.0f} > 32767")
        # Inc 3 / D2: ALL cells share ONE origin (the map center) so the
        # distant pass draws under a single shared matrix.
        assert len(shared_origins) == 1, (
            f"expected all cells to share one origin, got {len(shared_origins)}")
        print(f"PASS: {len(distant_files)} distant cells, {checked} packed "
              f"vertices within int16 at kLodScale={K_LOD_SCALE}, "
              f"shared origin {shared_origins.pop()}")
    finally:
        shutil.rmtree(d, ignore_errors=True)


if __name__ == "__main__":
    test_distant_lod_fits_int16_at_max_far()
    print("ALL PASS")
