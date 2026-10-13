#!/usr/bin/env python3
"""Validate strawberry conversion artifacts without an N64 runtime."""

from __future__ import annotations

import json
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def main() -> int:
    manifest = json.loads((ROOT / "assets/og_converted/asset_manifest.json").read_text())
    strawberry = next(a for a in manifest["assets"] if a["id"] == "strawberry")
    assert strawberry["profile"] == "static_model"
    assert strawberry["transform"].get("pivot") == "origin"
    output = ROOT / strawberry["outputs"][0]["path"]
    assert output.read_bytes()[:3] == b"T3M", "strawberry output is not T3DM"
    assert output.stat().st_size > 64, "strawberry T3DM is empty"
    sprite = ROOT / "assets/og_converted/textures/overworld/strawberry.sprite"
    assert sprite.exists() and sprite.stat().st_size > 16
    width, height = struct.unpack(">HH", sprite.read_bytes()[:4])
    assert width > 0 and height > 0 and width * height * 2 <= 4096
    print(f"static_model_asset_smoke: PASS ({width}x{height} strawberry texture)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
