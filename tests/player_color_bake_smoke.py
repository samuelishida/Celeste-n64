#!/usr/bin/env python3
"""Known UV samples remain stable for flat-color player preparation."""

from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from player_color_bake import sample_nearest_rgba


def main() -> int:
    # 2x2 RGBA pixels in Blender's bottom-left-origin order.
    pixels = (
        1.0, 0.0, 0.0, 0.25,  0.0, 1.0, 0.0, 0.25,
        0.0, 0.0, 1.0, 0.25,  1.0, 1.0, 1.0, 0.25,
    )
    assert sample_nearest_rgba(pixels, 2, 2, (0.1, 0.1)) == (1.0, 0.0, 0.0, 1.0)
    assert sample_nearest_rgba(pixels, 2, 2, (0.9, 0.1)) == (0.0, 1.0, 0.0, 1.0)
    assert sample_nearest_rgba(pixels, 2, 2, (0.1, 0.9)) == (0.0, 0.0, 1.0, 1.0)
    assert sample_nearest_rgba(pixels, 2, 2, (0.5, 0.5), (0.5, 1.0, 0.25, 0.5)) == (0.5, 1.0, 0.25, 1.0)
    try:
        sample_nearest_rgba(pixels, 2, 2, (float("nan"), 0.5))
        raise AssertionError("non-finite UV unexpectedly accepted")
    except ValueError:
        pass
    print("player color bake smoke: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
