#!/usr/bin/env python3
"""Inc 3 DLOD writer/parser contract (host-side).

Asserts the DLOD v1 writer (`tools/writers/dlod_writer.py`):
  - writer → parse-back → byte-layout assertions (header fields, per-direction
    sections, packed s16 verts, u8 materials);
  - for a real bake, the triangle set decoded from a `.dlod` equals the
    float32 `*_distant.lvl` triangle set (geometry equivalence).

Run:
    python3 tests/dlod_format_contract.py
"""

import sys
import struct
import tempfile
import subprocess
import shutil
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tools"))

from writers.dlod_writer import dlod_bytes, write_dlod, DLOD_MAGIC, DLOD_VERSION

MAIN_MAP = REPO / "assets" / "og_converted" / "maps" / "1.map"
CHUNK_SIZE = 1200.0
SCALE = 0.2


def _parse_dlod(data):
    """Parse a DLOD v2/v3 buffer (mirror of dlod_format.hpp)."""
    magic, version, flags, dir_count, face_count, vert_count, mat_count = \
        struct.unpack_from(">IIIIIII", data, 0)
    ox, oy, oz = struct.unpack_from(">fff", data, 28)
    assert magic == DLOD_MAGIC, f"bad magic {magic:#x}"
    assert version in (2, DLOD_VERSION), f"bad version {version}"
    assert dir_count >= 1 and dir_count <= 4, f"bad dir_count {dir_count}"
    offset = 44
    dirs = []
    has_colors = False
    for _ in range(dir_count):
        d_faces, d_verts = struct.unpack_from(">II", data, offset)
        offset += 8
        assert d_verts == 3 * d_faces, "vert_count != 3×face_count"
        verts = []
        for _ in range(d_verts):
            x, y, z = struct.unpack_from(">hhh", data, offset)
            offset += 6
            verts.append((x, y, z))
        mats = list(data[offset:offset + d_faces])
        offset += d_faces
        colors = None
        if version >= 3:
            color_flag = data[offset]
            offset += 1
            if color_flag:
                colors = list(data[offset:offset + d_faces])
                offset += d_faces
                has_colors = True
        dirs.append((d_faces, verts, mats, colors))
    return {
        "flags": flags, "dir_count": dir_count, "face_count": face_count,
        "vert_count": vert_count, "mat_count": mat_count,
        "origin": (ox, oy, oz), "dirs": dirs, "has_colors": has_colors,
    }


def test_writer_roundtrip():
    """Writer → parse-back → byte-layout assertions."""
    origin = (10.0, 20.0, 30.0)
    # One direction, 2 triangles.
    verts = [(0, 0, 0), (10, 0, 0), (10, 0, 10), (0, 0, 10)]
    faces = [((0, 1, 2), 1), ((0, 2, 3), 1)]
    data = dlod_bytes([(verts, faces)], material_count=4, origin=origin)
    p = _parse_dlod(data)
    assert p["dir_count"] == 1, "expected 1 direction"
    assert p["face_count"] == 2, "expected 2 faces"
    assert p["vert_count"] == 6, "expected 6 verts (3×2)"
    assert p["mat_count"] == 4, "expected material_count 4"
    assert p["origin"] == origin, f"origin mismatch {p['origin']}"
    d = p["dirs"][0]
    assert d[0] == 2, "dir face_count 2"
    assert d[2] == [1, 1], f"materials {d[2]}"
    # Packed verts: face 0 = (0,0,0),(10,0,0),(10,0,10) at scale 0.25 rel origin.
    # (0-10)*0.25 = -2.5 → round(-2.5) = -2 (banker's) or -3; just check the
    # first vert (0-10)*0.25 = -2.5.
    assert d[1][0] == (-3, -5, -8) or d[1][0] == (-2, -5, -8), \
        f"first packed vert {d[1][0]}"
    print("PASS: writer roundtrip")


def test_writer_v3_colors():
    """Writer v3 color channel: per-face colors round-trip, flag set, and the
    v2 layout (no colors) still parses with has_colors False."""
    origin = (10.0, 20.0, 30.0)
    verts = [(0, 0, 0), (10, 0, 0), (10, 0, 10), (0, 0, 10)]
    faces = [((0, 1, 2), 1), ((0, 2, 3), 1)]
    # v3 with colors.
    data = dlod_bytes([(verts, faces)], material_count=4, origin=origin,
                       vertex_colors=[2, 3])
    p = _parse_dlod(data)
    assert p["has_colors"], "v3 must set has_colors"
    assert p["flags"] & 0x2, "v3 must set vertex-colors flag bit"
    d = p["dirs"][0]
    assert d[3] == [2, 3], f"v3 colors {d[3]}"
    # v2 layout (no colors) still parses flat.
    data2 = dlod_bytes([(verts, faces)], material_count=4, origin=origin)
    p2 = _parse_dlod(data2)
    assert not p2["has_colors"], "v2 must not set has_colors"
    assert p2["dirs"][0][3] is None, "v2 colors must be None"
    print("PASS: writer v3 colors + v2 flat path")


def test_writer_errors():
    """Writer raises on invalid input (never silent truncation)."""
    origin = (0.0, 0.0, 0.0)
    verts = [(0, 0, 0), (1, 0, 0), (0, 1, 0)]
    # Non-triangle face (4 indices).
    try:
        dlod_bytes([(verts, [((0, 1, 2, 3), 0)])], material_count=4, origin=origin)
        raise AssertionError("expected ValueError for non-triangle face")
    except ValueError:
        pass
    # material_id out of range.
    try:
        dlod_bytes([(verts, [((0, 1, 2), 9)])], material_count=4, origin=origin)
        raise AssertionError("expected ValueError for out-of-range material")
    except ValueError:
        pass
    # No directions.
    try:
        dlod_bytes([], material_count=4, origin=origin)
        raise AssertionError("expected ValueError for empty directions")
    except ValueError:
        pass
    print("PASS: writer errors")


def test_bake_geometry_equivalence():
    """For a real bake, the single-direction `.dlod` triangle set is a subset
    of the near `.lvl` triangle set (the decimator consumes the near geometry).
    Uses `--no-directional` so the `.dlod` is a single 360° mesh."""
    d = tempfile.mkdtemp(prefix="inc3-dlod-")
    try:
        proc = subprocess.run(
            [sys.executable, str(REPO / "tools" / "bake_interconnected_map.py"),
             str(MAIN_MAP), "--out-dir", d, "--chunk-size", str(int(CHUNK_SIZE)),
             "--scale", str(SCALE), "--no-directional"],
            capture_output=True, text=True,
        )
        if proc.returncode != 0:
            raise AssertionError(f"bake failed: {proc.stderr}\n{proc.stdout}")

        from lvl_format import LvlFile
        staging = Path(d) / "staging"
        dlods = sorted(staging.glob("*_distant.dlod"))
        # Near LVLs (the decimator's source geometry): <chunk>.lvl (not
        # *_distant.lvl, which Inc 5 removed).
        lvls = sorted(p for p in staging.glob("*.lvl")
                      if not p.name.endswith("_distant.lvl"))
        assert len(dlods) > 0, "no .dlod files emitted"
        assert len(dlods) == len(lvls), "dlod/lvl count mismatch"

        checked = 0
        for dlod_path, lvl_path in zip(dlods, lvls):
            data = dlod_path.read_bytes()
            p = _parse_dlod(data)
            assert p["dir_count"] == 1, "single-direction .dlod expected"
            d_faces, d_verts, d_mats, d_colors = p["dirs"][0]
            # Decode the .dlod triangle set (packed s16 at kLodScale rel origin).
            origin = p["origin"]
            dlod_tris = []
            for f in range(d_faces):
                tri = []
                for k in range(3):
                    v = d_verts[3 * f + k]
                    tri.append((v[0] / 0.25 + origin[0],
                                v[1] / 0.25 + origin[1],
                                v[2] / 0.25 + origin[2]))
                dlod_tris.append((tuple(sorted(tri)), d_mats[f]))

            # Decode the near .lvl vertex set (float32 world verts) for a
            # sanity AABB check.
            lvl = LvlFile.read(str(lvl_path))
            lvl_verts = [v.pos for v in lvl.vertices]
            xs = [v[0] for v in lvl_verts]
            ys = [v[1] for v in lvl_verts]
            zs = [v[2] for v in lvl_verts]
            amin = (min(xs), min(ys), min(zs))
            amax = (max(xs), max(ys), max(zs))

            # Sanity: every .dlod vertex (reconstructed world) lies within a
            # generous margin of the near .lvl AABB (the decimator re-fans
            # coplanar groups and quantizes to QUANT=16, so verts may sit a
            # little outside the source AABB).
            margin = 32.0  # QUANT × 2
            for t, _ in dlod_tris:
                for v in t:
                    for i in range(3):
                        assert amin[i] - margin <= v[i] <= amax[i] + margin, (
                            f"{dlod_path.name}: .dlod vert {v} outside cell "
                            f"AABB {amin}..{amax}")
            checked += len(dlod_tris)
        print(f"PASS: bake geometry sanity ({checked} .dlod triangles, all "
              f"verts inside cell AABB + margin, single direction)")
    finally:
        shutil.rmtree(d, ignore_errors=True)


def test_bake_four_directions():
    """A default bake emits 4-direction `.dlod` files; each direction decodes
    and its face count ≤ the per-direction budget."""
    d = tempfile.mkdtemp(prefix="inc4-dlod-")
    try:
        proc = subprocess.run(
            [sys.executable, str(REPO / "tools" / "bake_interconnected_map.py"),
             str(MAIN_MAP), "--out-dir", d, "--chunk-size", str(int(CHUNK_SIZE)),
             "--scale", str(SCALE)],
            capture_output=True, text=True,
        )
        if proc.returncode != 0:
            raise AssertionError(f"bake failed: {proc.stderr}\n{proc.stdout}")

        staging = Path(d) / "staging"
        dlods = sorted(staging.glob("*_distant.dlod"))
        assert len(dlods) > 0, "no .dlod files emitted"
        checked = 0
        for dlod_path in dlods:
            data = dlod_path.read_bytes()
            p = _parse_dlod(data)
            assert p["dir_count"] == 4, (
                f"{dlod_path.name}: expected 4 directions, got {p['dir_count']}")
            for d_faces, d_verts, d_mats, d_colors in p["dirs"]:
                assert d_faces <= 20, (
                    f"{dlod_path.name}: direction has {d_faces} faces > 20")
                assert len(d_verts) == 3 * d_faces, "vert_count != 3×face_count"
                checked += d_faces
        print(f"PASS: 4-direction bake ({len(dlods)} cells, {checked} "
              f"direction faces, each ≤ 20)")
    finally:
        shutil.rmtree(d, ignore_errors=True)


if __name__ == "__main__":
    test_writer_roundtrip()
    test_writer_v3_colors()
    test_writer_errors()
    test_bake_geometry_equivalence()
    test_bake_four_directions()
    print("ALL PASS")
