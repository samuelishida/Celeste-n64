#!/usr/bin/env python3
"""Map-pack bake smoke test.

Asserts that a grid-chunked bake of an OG .map produces only cap-fitting
chunks and that the partition is lossless (every whole-map brush appears in
exactly one chunk).

Run:
    python3 tests/map_pack_smoke.py
"""

import sys
import os
import json
import shutil
import struct
import subprocess
from pathlib import Path
from typing import Optional, List

# Add tools to path for ogmap_lib imports.
REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tools"))

from ogmap_lib import parse_map

# Runtime caps (must match src/user/gameplay/world/level_loader.hpp + mappack_loader.hpp).
K_MAX_FACES = 1024
K_MAX_VERTICES = 8192
K_MAX_ROOMS = 64


def bake(tmp_out: str, map_path: str, chunk_size: float = 1200.0,
         extra: Optional[List[str]] = None) -> tuple:
    """Run bake_map_pack; return (chunks.json inventory, combined output)."""
    proc = subprocess.run(
        [
            sys.executable, str(REPO / "tools" / "bake_map_pack.py"),
            map_path,
            "--out-dir", tmp_out,
            "--chunk-size", str(chunk_size),
            "--scale", "0.2",
        ] + (extra or []),
        check=True,
        capture_output=True,
        text=True,
    )
    with open(Path(tmp_out) / "chunks.json") as f:
        return json.load(f), (proc.stdout or "") + (proc.stderr or "")


def test_main_a_side():
    """Bake the full main A-side 1.map; assert caps, room count, containment."""
    map_path = str(REPO / "assets" / "og_converted" / "maps" / "1.map")
    tmp_out = "/tmp/madeline-map-pack-smoke"
    inv, _ = bake(tmp_out, map_path, chunk_size=1200.0)

    chunks = inv["chunks"]
    assert inv["chunk_count"] == len(chunks), "chunk_count mismatch"
    # Corrected world-XZ axis at the default chunk_size 1200 gives exactly 47
    # non-empty cells for 1.map (the old up-axis 650 bake gave 60).
    assert inv["chunk_count"] == 47, (
        f"expected 47 chunks at --chunk-size 1200, got {inv['chunk_count']}"
    )
    assert inv["chunk_count"] <= K_MAX_ROOMS, (
        f"non-empty cell count {inv['chunk_count']} exceeds kMaxRooms {K_MAX_ROOMS}"
    )
    for c in chunks:
        assert c["faces"] <= K_MAX_FACES, (
            f"chunk {c['id']} faces={c['faces']} > cap {K_MAX_FACES}"
        )
        assert c["vertices"] <= K_MAX_VERTICES, (
            f"chunk {c['id']} vertices={c['vertices']} > cap {K_MAX_VERTICES}"
        )
        assert c["fits_caps"], f"chunk {c['id']} fits_caps is False"

    # Containment: every whole-map brush appears in exactly one chunk.
    pm = parse_map(map_path)
    whole = sum(len(e.brushes) for e in pm.entities)
    summed = sum(c["brush_count"] for c in chunks)
    assert summed == whole, (
        f"containment failed: sum chunk brushes {summed} != whole-map {whole}"
    )

    # chunks.json is valid JSON and has the expected fields.
    for c in chunks:
        assert "id" in c and "cell" in c and "sha256" in c

    print(f"PASS: 1.map → {inv['chunk_count']} chunks, all ≤ caps, containment OK "
          f"(whole={whole}, summed={summed})")


def test_b_side_single_room():
    """Bake the small B-side 1-1.map as a single-room submap; assert 1 chunk."""
    map_path = str(REPO / "assets" / "og_converted" / "maps" / "1-1.map")
    tmp_out = "/tmp/madeline-map-pack-smoke-bside"
    subprocess.run(
        [
            sys.executable, str(REPO / "tools" / "bake_map_pack.py"),
            map_path,
            "--out-dir", tmp_out,
            "--chunk-size", "1000",
            "--scale", "0.2",
            "--submap",
        ],
        check=True,
        capture_output=True,
    )
    with open(Path(tmp_out) / "chunks.json") as f:
        inv = json.load(f)
    assert inv["chunk_count"] == 1, f"B-side 1-1 should be 1 chunk, got {inv['chunk_count']}"
    c = inv["chunks"][0]
    assert c["faces"] <= K_MAX_FACES
    assert c["vertices"] <= K_MAX_VERTICES
    print(f"PASS: 1-1.map (B-side) → 1 chunk ({c['faces']} faces, {c['vertices']} verts)")


def test_cap_guard_fires():
    """A chunk_size too LARGE for 1.map's dense column must fatal (face cap)."""
    map_path = str(REPO / "assets" / "og_converted" / "maps" / "1.map")
    tmp_out = "/tmp/madeline-map-pack-smoke-cap"
    proc = subprocess.run(
        [
            sys.executable, str(REPO / "tools" / "bake_map_pack.py"),
            map_path,
            "--out-dir", tmp_out,
            "--chunk-size", "1500",  # dense column cell_n01_n02 exceeds 1024 faces
            "--scale", "0.2",
        ],
        capture_output=True,
        text=True,
    )
    assert proc.returncode != 0, "cap guard should have failed the bake"
    combined = (proc.stderr or "") + (proc.stdout or "")
    assert "exceeds runtime caps" in combined, (
        f"expected cap-guard message, got stderr={proc.stderr!r} stdout={proc.stdout!r}"
    )
    print("PASS: face-cap guard fires for oversized chunk")


def test_room_count_guard_fires():
    """A chunk_size too SMALL for 1.map must fatal (kMaxRooms overflow)."""
    map_path = str(REPO / "assets" / "og_converted" / "maps" / "1.map")
    tmp_out = "/tmp/madeline-map-pack-smoke-rooms"
    proc = subprocess.run(
        [
            sys.executable, str(REPO / "tools" / "bake_map_pack.py"),
            map_path,
            "--out-dir", tmp_out,
            "--chunk-size", "650",  # corrected axis: 118 cells > 64
            "--scale", "0.2",
        ],
        capture_output=True,
        text=True,
    )
    assert proc.returncode != 0, "room-count guard should have failed the bake"
    combined = (proc.stderr or "") + (proc.stdout or "")
    assert "exceeds runtime kMaxRooms" in combined, (
        f"expected room-count guard message, got stderr={proc.stderr!r} stdout={proc.stdout!r}"
    )
    print("PASS: room-count guard fires for undersized chunk")


def test_colmesh_reuse_off_by_default():
    """A fresh bake never reuses a colmesh; a stale leftover colmesh on a
    decoration-only chunk is unlinked (never silently shipped)."""
    map_path = str(REPO / "assets" / "og_converted" / "maps" / "1.map")
    tmp_out = "/tmp/madeline-map-pack-smoke-reuse"
    shutil.rmtree(tmp_out, ignore_errors=True)

    inv, out = bake(tmp_out, map_path, chunk_size=1200.0)
    assert "REUSE existing colmesh" not in out
    assert "using existing colmesh" not in out

    # Identify a decoration-only chunk (no collision triangles).
    deco = [c for c in inv["chunks"] if c["triangles"] == 0]
    assert deco, "expected at least one decoration-only chunk in 1.map"
    deco_id = deco[0]["id"]

    # Plant a stale .colmesh where a previous bake would have left one.
    stale = Path(tmp_out) / f"{deco_id}.colmesh"
    stale.write_bytes(b"CMSH" + b"\x00" * 64)
    assert stale.exists()

    # Re-bake into the SAME out dir without --reuse-colmesh: the stale file
    # must be removed (this chunk writes no colmesh).
    _, out2 = bake(tmp_out, map_path, chunk_size=1200.0)
    assert "REUSE existing colmesh" not in out2
    assert "stale colmesh" in out2, "expected a stale-unlink warning"
    assert not stale.exists(), f"stale colmesh {deco_id}.colmesh was not unlinked"
    print(f"PASS: stale colmesh unlinked on decoration-only chunk {deco_id}")

    shutil.rmtree(tmp_out, ignore_errors=True)


def test_colmesh_reuse_opt_in():
    """--reuse-colmesh keeps a planted colmesh and reports its REAL counts."""
    map_path = str(REPO / "assets" / "og_converted" / "maps" / "1.map")
    tmp_out = "/tmp/madeline-map-pack-smoke-reuse-on"
    shutil.rmtree(tmp_out, ignore_errors=True)

    inv, _ = bake(tmp_out, map_path, chunk_size=1200.0)
    deco = [c for c in inv["chunks"] if c["triangles"] == 0]
    assert deco
    deco_id = deco[0]["id"]

    # Plant a plausible stale colmesh: CMSH magic + a real-ish header.
    header = bytearray()
    header += b"CMSH"
    header += struct.pack(">HH", 1, 1)              # version, flags(has_bvh)
    header += struct.pack(">6h", 0, 0, 0, 0, 0, 0)  # aabb_min, aabb_max
    header += struct.pack(">f", 1.0)                # quant_scale
    header += struct.pack(">3f", 0.0, 0.0, 0.0)     # quant_origin
    header += struct.pack(">IIII", 3, 7, 5, 0)      # verts, tris, bvh, surf
    header += struct.pack(">IIII", 0, 0, 0, 0)      # offsets
    stale = Path(tmp_out) / f"{deco_id}.colmesh"
    stale.write_bytes(bytes(header))
    assert stale.exists()

    _, out2 = bake(tmp_out, map_path, chunk_size=1200.0,
                   extra=["--reuse-colmesh"])
    assert "REUSE existing colmesh" in out2, "expected REUSE log with --reuse-colmesh"
    with open(Path(tmp_out) / "chunks.json") as f:
        inv2 = json.load(f)
    c2 = next(c for c in inv2["chunks"] if c["id"] == deco_id)
    assert c2["triangles"] == 7, \
        f"expected real reused triangle count 7, got {c2['triangles']}"
    assert c2["bvh_nodes"] == 5, \
        f"expected real reused BVH count 5, got {c2['bvh_nodes']}"
    assert stale.exists(), "--reuse-colmesh should keep the planted colmesh"
    print(f"PASS: --reuse-colmesh kept colmesh on {deco_id}, real counts reported")

    shutil.rmtree(tmp_out, ignore_errors=True)


def main():
    test_main_a_side()
    test_b_side_single_room()
    test_cap_guard_fires()
    test_room_count_guard_fires()
    test_colmesh_reuse_off_by_default()
    test_colmesh_reuse_opt_in()
    print("\nAll map_pack_smoke tests passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())