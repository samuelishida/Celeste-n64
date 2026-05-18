#!/usr/bin/env python3
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).parent.parent / "tools"))
from level_bake_report import summarize

lines = summarize(Path("assets/og_converted/maps/1-1.map"), Path("filesystem/lvl/1-1.lvl"))
text = "\n".join(lines)
assert "baked_counts=colliders:402 faces:402 vertices:2048 entities:2" in text
assert "duplicate_vertex_faces=226" in text
assert "first_fan_degenerate_faces=202" in text
assert "reversed_winding_faces=402" in text
print(text)
print("level bake report smoke test: PASS")
