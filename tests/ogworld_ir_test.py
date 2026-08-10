#!/usr/bin/env python3
"""IR unit test (Inc 2).

Asserts that two independent IR builds from the same source have identical
stable ids, policy summaries, and serialized report content.

Run:
    python3 tests/ogworld_ir_test.py
"""

import sys
import json
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tools"))

from ogworld.parse import build_world_ir
from ogworld.class_policy import validate_policies, summarize_policies
from ogworld.model import WorldBuild


def serialize_ir(build: WorldBuild) -> dict:
    """Deterministic serialization of the IR for comparison."""
    return {
        "source_sha256": build.source_sha256,
        "texture_manifest": list(build.texture_manifest),
        "spawns": [
            {
                "kind": s.kind,
                "source_id": s.source_id,
                "name": s.name,
                "classname": s.classname,
                "position": list(s.position),
            }
            for s in build.spawns
        ],
        "brush_cell_counts": sorted(
            (f"{k[0]}:{k[1]}", v) for k, v in build.brush_cell_counts.items()
        ),
        "policy_summary": build.policy_summary,
    }


def test_deterministic_ir():
    """Two independent IR builds from the same source are identical."""
    map_path = str(REPO / "assets" / "og_converted" / "maps" / "1.map")
    a = build_world_ir(map_path, scale=0.2, strict=True)
    b = build_world_ir(map_path, scale=0.2, strict=True)
    sa = serialize_ir(a)
    sb = serialize_ir(b)
    assert sa == sb, "two IR builds differ"
    print(f"PASS: deterministic IR — {len(a.spawns)} spawns, "
          f"{len(a.texture_manifest)} textures")


def test_spawn_counts():
    """1.map has 11 PlayerSpawns (1 Start), 20 Strawberries, 10 Cassettes,
    8 Refills, 6 Springs."""
    map_path = str(REPO / "assets" / "og_converted" / "maps" / "1.map")
    build = build_world_ir(map_path, scale=0.2, strict=True)
    from collections import Counter
    kinds = Counter(s.kind for s in build.spawns)
    classes = Counter(s.classname for s in build.spawns)
    assert kinds["start"] == 1, f"expected 1 Start, got {kinds['start']}"
    assert kinds["anchor"] == 10, f"expected 10 anchors, got {kinds['anchor']}"
    assert classes["Strawberry"] == 20, f"got {classes['Strawberry']} strawberries"
    assert classes["Cassette"] == 10, f"got {classes['Cassette']} cassettes"
    assert classes["Refill"] == 8, f"got {classes['Refill']} refills"
    assert classes["Spring"] == 6, f"got {classes['Spring']} springs"
    print(f"PASS: spawn counts — start={kinds['start']} anchor={kinds['anchor']} "
          f"strawberry={classes['Strawberry']} cassette={classes['Cassette']}")


def test_policy_coverage():
    """Every brush-bearing class in 1.map has an explicit policy."""
    map_path = str(REPO / "assets" / "og_converted" / "maps" / "1.map")
    from ogmap_lib import parse_map
    pm = parse_map(map_path)
    errors = validate_policies(pm, strict=True)
    assert not errors, f"policy gaps: {errors}"
    print("PASS: all brush-bearing classes have explicit policy")


def test_fixture_ir():
    """The 2x2 fixture IR has 2 spawns (Start + anchor) and 1 actor."""
    map_path = str(REPO / "tests" / "fixtures" / "interconnected-2x2.map")
    build = build_world_ir(map_path, scale=0.2, strict=True)
    kinds = [s.kind for s in build.spawns]
    assert kinds.count("start") == 1, f"expected 1 start, got {kinds}"
    assert kinds.count("anchor") == 1, f"expected 1 anchor, got {kinds}"
    assert any(s.classname == "Strawberry" for s in build.spawns)
    print(f"PASS: fixture IR — spawns={kinds}")


def main() -> int:
    test_deterministic_ir()
    test_spawn_counts()
    test_policy_coverage()
    test_fixture_ir()
    print("\nAll ogworld_ir_test tests passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
