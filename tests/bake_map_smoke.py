#!/usr/bin/env python3
"""Smoke test for bake_map.py — verify brush parsing and polygon generation."""

import sys
import math
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent.parent / "tools"))

from bake_map import parse_map_file, compute_face_polygon, vdot

def test_brush_separation():
    """Verify worldspawn brushes are parsed as separate brushes, not merged."""
    entities = parse_map_file("assets/og_converted/maps/1-1.map")
    ws = entities[0]
    brushes = ws["brushes"]
    assert len(brushes) >= 5, f"Expected >= 5 brushes, got {len(brushes)}"
    for i, b in enumerate(brushes):
        assert len(b) >= 4, f"Brush {i} has only {len(b)} planes (need >= 4)"
    print(f"PASS: {len(brushes)} brushes parsed correctly")
    return True

def test_face_polygon():
    """Verify that per-face polygon clipping produces valid convex polygons."""
    entities = parse_map_file("assets/og_converted/maps/1-1.map")
    ws = entities[0]
    b0 = ws["brushes"][0]  # first box brush

    # Each face should produce a valid polygon
    face_count = 0
    for face_idx in range(len(b0)):
        polygon = compute_face_polygon(b0, face_idx)
        if len(polygon) >= 3:
            face_count += 1

    assert face_count >= 4, f"Box brush should have >= 4 valid faces, got {face_count}"
    print(f"PASS: {face_count} valid face polygons from box brush")
    return True

def test_bake_output():
    """Verify the baked LVL file has reasonable face/vertex counts."""
    from lvl_format import LvlFile

    lvl = LvlFile.read("filesystem/lvl/1-1.lvl")

    assert len(lvl.faces) > 0, "No faces in LVL file"
    assert len(lvl.vertices) > 0, "No vertices in LVL file"
    assert len(lvl.entities) > 0, "No entities in LVL file"

    # Face count should be in reasonable range (not 8 = broken, not 10000 = way too many)
    assert len(lvl.faces) >= 50, f"Too few faces: {len(lvl.faces)}"
    assert len(lvl.faces) <= 2000, f"Too many faces: {len(lvl.faces)}"

    print(f"PASS: LVL has {len(lvl.faces)} faces, {len(lvl.vertices)} vertices, {len(lvl.entities)} entities")
    return True

if __name__ == "__main__":
    all_pass = True
    all_pass &= test_brush_separation()
    all_pass &= test_face_polygon()
    all_pass &= test_bake_output()

    if all_pass:
        print("bake_map smoke test: PASS")
        sys.exit(0)
    else:
        print("bake_map smoke test: FAIL")
        sys.exit(1)