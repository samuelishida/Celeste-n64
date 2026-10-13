"""Host-safe helpers for baking opaque nearest-sampled player vertex colors."""

from __future__ import annotations

import math
from typing import Sequence


def sample_nearest_rgba(
    pixels: Sequence[float],
    width: int,
    height: int,
    uv: tuple[float, float],
    factor: tuple[float, float, float, float] = (1.0, 1.0, 1.0, 1.0),
) -> tuple[float, float, float, float]:
    """Sample Blender-style bottom-left-origin image pixels at one UV."""
    if width <= 0 or height <= 0 or len(pixels) < width * height * 4:
        raise ValueError("invalid player color image buffer")
    if len(uv) != 2 or not all(math.isfinite(float(value)) for value in uv):
        raise ValueError("non-finite player color UV")
    if len(factor) != 4 or not all(math.isfinite(float(value)) for value in factor):
        raise ValueError("invalid player material color factor")
    x = min(width - 1, max(0, int(math.floor(float(uv[0]) * width))))
    y = min(height - 1, max(0, int(math.floor(float(uv[1]) * height))))
    offset = (y * width + x) * 4
    return tuple(
        max(0.0, min(1.0, float(pixels[offset + channel]) * float(factor[channel])))
        for channel in range(3)
    ) + (1.0,)
