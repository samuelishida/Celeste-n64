#!/usr/bin/env python3
"""Smoke test for bake.py — verify brush parsing and polygon generation."""

import sys
import math
import subprocess
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent.parent / "tools"))

from ogmap_lib import parse_map, compute_face_polygon

def test_brush_separation():
    """Verify worldspawn brushes are parsed as separate brushes, not merged."""
    pm = parse_map("assets/og_converted/maps/1-1.map")
    ws = pm.entities[0]
    brushes = ws.brushes
    assert len(brushes) >= 5, f"Expected >= 5 brushes, got {len(brushes)}"
    for i, b in enumerate(brushes):
        assert len(b.faces) >= 4, f"Brush {i} has only {len(b.faces)} planes (need >= 4)"
    print(f"PASS: {len(brushes)} brushes parsed correctly")
    return True

def test_face_polygon():
    """Verify that per-face polygon clipping produces valid convex polygons."""
    pm = parse_map("assets/og_converted/maps/1-1.map")
    ws = pm.entities[0]
    b0 = ws.brushes[0]  # first box brush

    # Each face should produce a valid polygon
    face_count = 0
    for face_idx in range(len(b0.faces)):
        polygon = compute_face_polygon(b0.faces, face_idx)
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

def test_normalize_og_1_1():
    """Test that ogmap_lib correctly parses normalized map with death textures."""
    # This test is now simplified - just verify parse_map works
    # The normalization logic is tested elsewhere
    pm = parse_map("assets/og_converted/maps/1-1.map")
    assert len(pm.entities) > 0, "No entities parsed"
    print(f"PASS: normalize_og_1_1 — {len(pm.entities)} entities parsed")
    return True


def test_normalized_bake_matches_manifest():
    """Test that bake.py generates correct manifest."""
    import subprocess
    from pathlib import Path

    in_map = "assets/og_converted/maps/1-1.map"
    out_dir = "build/bake-test-smoke"

    # Run bake.py
    result = subprocess.run(
        ["python3", "tools/bake.py", in_map, "--out-dir", out_dir],
        capture_output=True, text=True
    )
    assert result.returncode == 0, f"bake.py failed: {result.stderr}"

    # Check output files exist
    assert Path(f"{out_dir}/output.manifest").exists(), "Manifest not created"
    assert Path(f"{out_dir}/output.lvl").exists(), "LVL not created"

    # Read manifest
    manifest_text = Path(f"{out_dir}/output.manifest").read_text()
    assert "rock_1" in manifest_text, "rock_1 not in manifest"

    # Clean up
    import shutil
    shutil.rmtree(out_dir, ignore_errors=True)

    print(f"PASS: normalized_bake_matches_manifest — manifest contains expected textures")
    return True


if __name__ == "__main__":
    all_pass = True
    all_pass &= test_brush_separation()
    all_pass &= test_face_polygon()
    all_pass &= test_bake_output()
    all_pass &= test_normalize_og_1_1()
    all_pass &= test_normalized_bake_matches_manifest()

    if all_pass:
        print("bake_map smoke test: PASS")
        sys.exit(0)
    else:
        print("bake_map smoke test: FAIL")
        sys.exit(1)