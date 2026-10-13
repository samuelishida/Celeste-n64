#!/usr/bin/env python3
"""Validate whole-map texture profiles and per-axis UV conditioning."""

from __future__ import annotations

import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from asset_manifest import load_manifest, texture_profiles  # noqa: E402
from convert_og_assets import resize_texture_image  # noqa: E402
from ogmap_lib import parse_map  # noqa: E402
from ogmap_lib.texture_mapping import compute_uv  # noqa: E402


def main() -> int:
    manifest = load_manifest(ROOT / "assets/og_converted/asset_manifest.json")
    profiles = texture_profiles(manifest, ROOT)
    parsed = parse_map(str(ROOT / "assets/og_converted/maps/1.map"))
    materials = []
    for entity in parsed.entities:
        for brush in entity.brushes:
            for face in brush.faces:
                if face["texture"] not in materials:
                    materials.append(face["texture"])
    assert len(materials) == 32, f"unexpected map material count: {len(materials)}"
    for name in materials:
        assert name in profiles, f"missing profile for map material {name}"
        profile = profiles[name]
        if profile["name"] == "none":
            continue
        if profile["name"] == "world_repeat_rgba16":
            assert profile["filter"] == "bilinear", f"{name}: expected bilinear minification filter"
            assert profile["mipmap"] == "BOX", f"{name}: expected box-filter mip chain"
            assert profile["resize_filter"] == "BOX", f"{name}: source downsample must prefilter repeating texels"
            cap = 16 if max(profile["source_width"], profile["source_height"]) > 64 else 32
            assert max(profile["output_width"], profile["output_height"]) <= cap, (
                f"{name}: output exceeds source-tier cap {cap}px"
            )
        elif profile["name"] == "world_decal_rgba16":
            assert profile["filter"] == "bilinear", f"{name}: expected bilinear decal filtering"
            assert profile["mipmap"] == "NONE", f"{name}: keep alpha decals unmipped"
            assert profile["resize_filter"] == "NEAREST", f"{name}: preserve straight-alpha edges"
            assert max(profile["output_width"], profile["output_height"]) <= 32, (
                f"{name}: decal output cap changed"
            )
        payload = profile["output_width"] * profile["output_height"] * 2
        assert payload <= 4096, f"{name}: RGBA16 payload {payload} > TMEM"
        if profile["mipmap"] == "BOX":
            width, height = profile["output_width"], profile["output_height"]
            mip_payload = width * height * 2
            while min(width // 2, height // 2) >= 4:
                width, height = width // 2, height // 2
                mip_payload += width * height * 2
            assert mip_payload <= 4096, f"{name}: mip chain {mip_payload} bytes exceeds TMEM"
        source = next(a["source"] for a in manifest["assets"] if a.get("id") == name)
        with Image.open(ROOT / source) as image:
            assert image.size == (profile["source_width"], profile["source_height"])
        face = {"normal": (0, 1, 0), "p1": (0, 0, 0), "scale_u": 1, "scale_v": 1}
        plain = compute_uv((8, 0, 4), face)
        scaled = compute_uv((8, 0, 4), face,
                            uv_scale=(profile["uv_scale_u"], profile["uv_scale_v"]))
        assert abs(scaled[0] - plain[0] * profile["uv_scale_u"]) < 1e-6
        assert abs(scaled[1] - plain[1] * profile["uv_scale_v"]) < 1e-6
    assert profiles["__TB_empty"]["name"] == "none"
    assert profiles["invisible"]["name"] == "none"
    for sentinel in ("__TB_empty", "invisible"):
        assert profiles[sentinel]["filter"] == "none"
        assert profiles[sentinel]["wrap_s"] == "none"
        assert profiles[sentinel]["wrap_t"] == "none"
        assert not profiles[sentinel].get("rom_path")

    # Prove the declared world-repeat resize filter is what conversion uses:
    # a 1-pixel checkerboard must average away when reduced 4:1, rather than
    # being aliased into another high-frequency 16x16 tile.
    checker = Image.new("RGBA", (64, 64))
    checker.putdata([
        (255, 255, 255, 255) if (x + y) % 2 else (0, 0, 0, 255)
        for y in range(64) for x in range(64)
    ])
    filtered = resize_texture_image(checker, (16, 16), profiles["snow_1"])
    assert all(max(pixel[:3]) - min(pixel[:3]) <= 1 for pixel in filtered.getdata())
    assert all(126 <= pixel[0] <= 129 for pixel in filtered.getdata())
    print(f"texture_profile_smoke: PASS ({len(materials)} map materials)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
