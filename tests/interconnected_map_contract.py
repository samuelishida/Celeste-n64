#!/usr/bin/env python3
"""Interconnected map contract test (Inc 1 — baseline only).

Establishes the deterministic baseline for the full-world rebuild:

1. Parses `assets/og_converted/maps/1.map` with the existing parser and
   records entity/brush/class counts plus the source SHA-256.
2. Asserts the small `tests/fixtures/interconnected-2x2.map` fixture
   partitions into exactly four connected world-XZ cells, has a floor
   crossing each seam, one overhanging brush, one named `Start` anchor, and
   one actor spawn.
3. Records the current scene-integration gap: the report notes that the
   future v2 boot gate requires a global collision artifact (implemented in
   Inc 6), and that `GameplayScene` still routes its hot path through the
   legacy single `Room`.

This increment is baseline-only: it uses the existing parser and current
bake inventory. It does NOT import the Inc 2 IR or the Inc 6 runtime API.
Inc 2 must later make its report match this baseline.

Run:
    python3 tests/interconnected_map_contract.py
"""

import sys
import hashlib
import json
import subprocess
import shutil
import tempfile
from pathlib import Path
from collections import Counter

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tools"))

from ogmap_lib import parse_map
from ogmap_lib.brush_grid import (
    partition_parsed_map, cell_id, brush_center, cell_of,
)

# The full A-side source map.
MAIN_MAP = REPO / "assets" / "og_converted" / "maps" / "1.map"
# The small 2x2 fixture.
FIXTURE_MAP = REPO / "tests" / "fixtures" / "interconnected-2x2.map"

# Expected baseline counts for 1.map (from the plan's evidence).
EXPECTED_ENTITIES = 706
EXPECTED_BRUSHES = 1182

# Fixture grid parameters (must match the fixture authoring).
FIXTURE_CHUNK_SIZE = 1000.0
FIXTURE_SCALE = 0.2
# The four expected cells (world-XZ grid indices).
EXPECTED_CELLS = {(0, 0), (1, 0), (0, -1), (1, -1)}


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(8192), b""):
            h.update(chunk)
    return h.hexdigest()


def load_report() -> dict:
    """Return the deterministic baseline report for 1.map."""
    pm = parse_map(str(MAIN_MAP))
    brushes = sum(len(e.brushes) for e in pm.entities)
    classes = Counter(e.classname for e in pm.entities)
    return {
        "source": str(MAIN_MAP),
        "source_sha256": sha256(MAIN_MAP),
        "entities": len(pm.entities),
        "brushes": brushes,
        "classes": dict(sorted(classes.items())),
        "textures": len(pm.textures),
    }


def assert_fixture_contract() -> dict:
    """Assert the 2x2 fixture's four cells, seam coverage, and named spawns.

    Returns a report dict describing the fixture.
    """
    pm = parse_map(str(FIXTURE_MAP))
    cells = partition_parsed_map(pm, FIXTURE_CHUNK_SIZE, FIXTURE_SCALE)

    # 1. Exactly four connected cells.
    cell_keys = set(cells.keys())
    assert cell_keys == EXPECTED_CELLS, (
        f"fixture cells {cell_keys} != expected {EXPECTED_CELLS}"
    )

    # 2. Every cell is reachable from cell (0,0) via ±X/±Z adjacency.
    reachable = {(0, 0)}
    frontier = [(0, 0)]
    while frontier:
        cur = frontier.pop()
        for dx, dz in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            nb = (cur[0] + dx, cur[1] + dz)
            if nb in cells and nb not in reachable:
                reachable.add(nb)
                frontier.append(nb)
    assert reachable == EXPECTED_CELLS, (
        f"fixture not fully connected: reachable {reachable} != {EXPECTED_CELLS}"
    )

    # 3. A floor crosses each seam. The four floor brushes are the first four
    #    worldspawn brushes; each spans a full cell and touches its neighbors
    #    at the shared edge. Verify each cell has a floor brush whose AABB
    #    center is in that cell.
    ws = pm.entities[0]
    assert ws.classname == "worldspawn"
    floor_centers = {cell_of(brush_center(b), FIXTURE_CHUNK_SIZE, FIXTURE_SCALE)
                     for b in ws.brushes[:4]}
    assert floor_centers == EXPECTED_CELLS, (
        f"floor brush cells {floor_centers} != {EXPECTED_CELLS}"
    )

    # 4. One overhanging brush spans the seam between cell (0,0) and (1,0).
    #    Its AABB must intersect both cells' columns (world XZ).
    overhang = ws.brushes[4]
    c = brush_center(overhang)
    # The overhang spans map_x[900,1100]; its center x=1000 sits exactly on
    # the seam. Its AABB must intersect both cell (0,0) and (1,0) columns.
    # We assert the brush's world-XZ AABB overlaps both cells.
    from ogmap_lib.brush_grid import compute_brush_aabb
    (mn, mx) = compute_brush_aabb(overhang)
    # world x range = [mn[0]*s, mx[0]*s]; cell width = chunk_size*scale = 200.
    wx0, wx1 = mn[0] * FIXTURE_SCALE, mx[0] * FIXTURE_SCALE
    cell_w = FIXTURE_CHUNK_SIZE * FIXTURE_SCALE
    ix0 = int(wx0 // cell_w)
    ix1 = int(wx1 // cell_w)
    assert ix0 <= 0 <= ix1, f"overhang x range {wx0}..{wx1} does not span cell 0"
    assert ix0 < ix1, f"overhang does not span two cells (ix {ix0}..{ix1})"
    assert c[0] == 1000.0, f"overhang center x {c[0]} != 1000 (seam)"

    # 5. Named spawns: exactly one 'Start' PlayerSpawn and one actor spawn.
    spawns = [e for e in pm.entities if e.classname == "PlayerSpawn"]
    assert len(spawns) == 2, f"expected 2 PlayerSpawns, got {len(spawns)}"
    names = [e.properties.get("name", "") for e in spawns]
    assert "Start" in names, f"missing named Start anchor: {names}"
    actors = [e for e in pm.entities if e.classname == "Strawberry"]
    assert len(actors) == 1, f"expected 1 Strawberry, got {len(actors)}"

    return {
        "source": str(FIXTURE_MAP),
        "source_sha256": sha256(FIXTURE_MAP),
        "cells": sorted(cell_id(k) for k in cells),
        "cell_keys": sorted(cells.keys()),
        "spawn_names": names,
        "actor_count": len(actors),
        "overhang_center": c,
    }


def bake_fixture_smoke() -> dict:
    """Bake the 2x2 fixture through the existing bake_map_pack pipeline.

    Asserts the fixture produces exactly 4 cap-fitting chunks (the current
    center-ownership partition). This is baseline evidence only; Inc 3
    replaces it with overlap/clipping partition.
    """
    tmp = Path(tempfile.mkdtemp(prefix="interconnected-fixture-bake-"))
    try:
        proc = subprocess.run(
            [
                sys.executable, str(REPO / "tools" / "bake_map_pack.py"),
                str(FIXTURE_MAP),
                "--out-dir", str(tmp),
                "--chunk-size", str(int(FIXTURE_CHUNK_SIZE)),
                "--scale", str(FIXTURE_SCALE),
            ],
            capture_output=True, text=True,
        )
        if proc.returncode != 0:
            raise AssertionError(
                f"fixture bake failed: {proc.stderr}\n{proc.stdout}"
            )
        with open(tmp / "chunks.json") as f:
            inv = json.load(f)
        chunks = inv["chunks"]
        assert inv["chunk_count"] == 4, (
            f"fixture bake produced {inv['chunk_count']} chunks, expected 4"
        )
        for c in chunks:
            assert c["faces"] <= 1024, f"chunk {c['id']} faces over cap"
            assert c["vertices"] <= 8192, f"chunk {c['id']} vertices over cap"
            assert c["fits_caps"], f"chunk {c['id']} does not fit caps"
        return {
            "chunk_count": inv["chunk_count"],
            "chunk_ids": sorted(c["id"] for c in chunks),
        }
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def main() -> int:
    report = load_report()

    # Baseline counts must match the plan's evidence.
    assert report["entities"] == EXPECTED_ENTITIES, (
        f"1.map entities {report['entities']} != {EXPECTED_ENTITIES}"
    )
    assert report["brushes"] == EXPECTED_BRUSHES, (
        f"1.map brushes {report['brushes']} != {EXPECTED_BRUSHES}"
    )

    # Class policy coverage: every brush-bearing class must have a ClassDef
    # or a skip reason (the existing registry). Unknown brush classes are a
    # hard failure for the full-world gate.
    from ogmap_lib import classify_entity, is_skipped
    pm = parse_map(str(MAIN_MAP))
    unhandled = []
    for ent in pm.entities:
        if not ent.brushes:
            continue
        if classify_entity(ent) is None and is_skipped(ent) is None:
            unhandled.append(ent.classname)
    assert not unhandled, f"unhandled brush classes in 1.map: {unhandled}"

    fixture = assert_fixture_contract()

    # Fixture host smoke build: bake the fixture through the existing
    # bake_map_pack pipeline and assert it produces 4 cap-fitting chunks.
    fixture_bake = bake_fixture_smoke()

    # The report records the scene-integration gap (baseline evidence).
    report["fixture"] = fixture
    report["fixture_bake"] = fixture_bake
    report["scene_integration_gap"] = (
        "GameplayScene::Update/Render still route through the legacy single "
        "Room; the v2 boot gate (Inc 6) will require a global collision "
        "artifact."
    )

    out = REPO / "build" / "interconnected_contract_report.json"
    out.parent.mkdir(parents=True, exist_ok=True)
    with open(out, "w") as f:
        json.dump(report, f, indent=2)

    print(f"PASS: 1.map = {report['entities']} entities / {report['brushes']} brushes")
    print(f"PASS: source sha256 = {report['source_sha256'][:16]}...")
    print(f"PASS: fixture cells = {fixture['cells']}")
    print(f"PASS: fixture spawns = {fixture['spawn_names']}, "
          f"actors = {fixture['actor_count']}")
    print(f"PASS: overhang center = {fixture['overhang_center']}")
    print(f"PASS: fixture bake = {fixture_bake['chunk_count']} chunks, "
          f"all ≤ caps")
    print(f"report -> {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
