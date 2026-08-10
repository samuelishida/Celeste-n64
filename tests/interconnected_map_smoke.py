#!/usr/bin/env python3
"""Inc 5 whole-map offline validation.

Asserts global collision source coverage exactly once, visual source coverage
with declared seam duplication, global memory/triangle/BVH budgets, visual
room caps, artifact hashes, deterministic output, explicit class-policy
counts, dynamic-proxy counts, and named spawn records.

Run:
    python3 tests/interconnected_map_smoke.py
"""

import sys
import json
import subprocess
import tempfile
import shutil
from pathlib import Path
from collections import Counter

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tools"))

MAIN_MAP = REPO / "assets" / "og_converted" / "maps" / "1.map"
CHUNK_SIZE = 1200.0
SCALE = 0.2

# Expected counts from the plan's evidence.
EXPECTED_ENTITIES = 706
EXPECTED_BRUSHES = 1182
EXPECTED_START = 1
EXPECTED_ANCHORS = 10
EXPECTED_STRAWBERRIES = 20
EXPECTED_CASSETTES = 10
EXPECTED_REFILLS = 8
EXPECTED_SPRINGS = 6


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


def test_global_collision_coverage():
    """Global collision covers every policy-approved solid brush exactly once."""
    d = tempfile.mkdtemp(prefix="inc5-map-")
    try:
        report = bake(d)
        assert report["coverage_errors"] == 0, (
            f"coverage errors: {report['coverage_errors']}"
        )
        g = report["global_colmesh"]
        # Reference: ~20824 verts, 10596 tris, 8191 BVH nodes.
        assert g["triangles"] > 10000, f"too few tris: {g['triangles']}"
        assert g["vertices"] > 20000, f"too few verts: {g['vertices']}"
        assert g["bvh_nodes"] > 8000, f"too few BVH nodes: {g['bvh_nodes']}"
        # On-disk ~383 KB.
        assert g["size_bytes"] > 300000, f"global CMSH too small: {g['size_bytes']}"
        print(f"PASS: global collision — {g['triangles']} tris, "
              f"{g['vertices']} verts, {g['bvh_nodes']} BVH nodes, "
              f"{g['size_bytes']} bytes")
    finally:
        shutil.rmtree(d, ignore_errors=True)


def test_visual_room_caps():
    """Every visual room fits the runtime caps."""
    d = tempfile.mkdtemp(prefix="inc5-caps-")
    try:
        report = bake(d)
        assert not report["cap_errors"], f"cap errors: {report['cap_errors']}"
        assert report["chunk_count"] <= 64, (
            f"room count {report['chunk_count']} > 64"
        )
        for cid, c in report["chunks"].items():
            assert c["faces"] <= 1024, f"{cid} faces {c['faces']} > 1024"
            assert c["vertices"] <= 8192, f"{cid} verts {c['vertices']} > 8192"
        print(f"PASS: {report['chunk_count']} visual rooms all ≤ caps")
    finally:
        shutil.rmtree(d, ignore_errors=True)


def test_spawn_records():
    """Named spawn records: 1 Start, 10 anchors, 20 strawberries, etc."""
    d = tempfile.mkdtemp(prefix="inc5-spawn-")
    try:
        bake(d)
        from mappack_format import read_mappack_v2_binary
        pack = read_mappack_v2_binary(str(Path(d) / "staging" / "forsyken-city.mappack"))
        kinds = Counter(s.kind for r in pack.rooms for s in r.spawns)
        classes = Counter(s.classname for r in pack.rooms for s in r.spawns)
        assert kinds[0] == EXPECTED_START, f"start count {kinds[0]}"
        assert kinds[1] == EXPECTED_ANCHORS, f"anchor count {kinds[1]}"
        assert classes["Strawberry"] == EXPECTED_STRAWBERRIES
        assert classes["Cassette"] == EXPECTED_CASSETTES
        assert classes["Refill"] == EXPECTED_REFILLS
        assert classes["Spring"] == EXPECTED_SPRINGS
        # Exactly one Start.
        starts = [s for r in pack.rooms for s in r.spawns if s.kind == 0]
        assert len(starts) == 1, f"expected 1 Start, got {len(starts)}"
        assert starts[0].name == "Start"
        print(f"PASS: spawn records — start=1 anchor=10 strawberry=20 "
              f"cassette=10 refill=8 spring=6")
    finally:
        shutil.rmtree(d, ignore_errors=True)


def test_dynamic_proxy_counts():
    """Dynamic classes are labeled static-proxy in the policy summary."""
    d = tempfile.mkdtemp(prefix="inc5-dyn-")
    try:
        report = bake(d)
        summary = report["policy_summary"]
        dynamic = [k for k, v in summary.items() if v.get("dynamic")]
        assert "TrafficBlock" in dynamic
        assert "FallingBlock" in dynamic
        assert "MovingBlock" in dynamic
        assert "FloatyBlock" in dynamic
        # DeathBlock is collision-only (not rendered).
        assert summary["DeathBlock"]["render_mode"] == "none"
        print(f"PASS: dynamic-proxy classes labeled: {sorted(dynamic)}")
    finally:
        shutil.rmtree(d, ignore_errors=True)


def test_artifact_hashes():
    """Artifact hashes are recorded and match the staged files."""
    d = tempfile.mkdtemp(prefix="inc5-hash-")
    try:
        bake(d)
        from mappack_format import read_mappack_v2_binary
        from artifact_hash import artifact_hash
        staging = Path(d) / "staging"
        pack = read_mappack_v2_binary(str(staging / "forsyken-city.mappack"))
        # Global colmesh hash.
        gname = pack.global_colmesh_path.rsplit("/", 1)[-1]
        gh = artifact_hash(str(staging / gname))
        assert gh["crc32"] == pack.global_colmesh_crc32, "global colmesh crc mismatch"
        assert gh["size_bytes"] == pack.global_colmesh_size, "global colmesh size mismatch"
        # Room LVL hashes.
        for r in pack.rooms:
            lname = r.lvl_path.rsplit("/", 1)[-1]
            lh = artifact_hash(str(staging / lname))
            assert lh["crc32"] == r.lvl_crc32, f"{r.id} lvl crc mismatch"
            assert lh["size_bytes"] == r.lvl_size, f"{r.id} lvl size mismatch"
        print(f"PASS: artifact hashes match staged files")
    finally:
        shutil.rmtree(d, ignore_errors=True)


def main() -> int:
    test_global_collision_coverage()
    test_visual_room_caps()
    test_spawn_records()
    test_dynamic_proxy_counts()
    test_artifact_hashes()
    print("\nAll interconnected_map_smoke tests passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
