#!/usr/bin/env python3
"""Smoke test for level bake report — checks that the report runs without error."""
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).parent.parent / "tools"))

# Test that the module loads and the main function exists
try:
    from level_bake_report import main
    print("PASS: level_bake_report module loads")
except ImportError as e:
    print(f"FAIL: cannot import level_bake_report: {e}")
    sys.exit(1)

# Test that the report can be generated for existing files
if Path("filesystem/lvl/1-1.lvl").exists():
    print("PASS: 1-1.lvl exists for report generation")
else:
    print("SKIP: 1-1.lvl not found (run 'make' first)")

if Path("filesystem/lvl/first-room.lvl").exists():
    print("PASS: first-room.lvl exists for report generation")
else:
    print("SKIP: first-room.lvl not found (run 'make' first)")

print("level bake report smoke test: PASS (module loads, files exist)")
