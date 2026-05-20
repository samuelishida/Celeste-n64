#!/usr/bin/env python3
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).parent.parent / "tools"))
from level_bake_report import summarize

lines = summarize(Path("assets/rooms/first-room/first-room.map"), Path("filesystem/lvl/first-room.lvl"))
text = "\n".join(lines)
assert "baked_counts=colliders:54 faces:54 vertices:216 entities:2" in text
assert "duplicate_vertex_faces=0" in text
assert "first_fan_degenerate_faces=0" in text
assert "reversed_winding_faces=0" in text
assert "materials=['rock_1', 'rock_1_climbable']" in text
print(text)
print("level bake report smoke test: PASS")
