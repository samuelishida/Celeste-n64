#!/usr/bin/env python3
"""Rights-safe deterministic static-player preparation smoke."""

from __future__ import annotations

import io
import json
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

from PIL import Image


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from glb_assets import read_glb


def _fixture(path: Path) -> None:
    image_buffer = io.BytesIO()
    image = Image.new("RGBA", (8, 8), (210, 30, 120, 255))
    image.save(image_buffer, format="PNG", optimize=False)
    image_bytes = image_buffer.getvalue()
    positions = struct.pack("<9f", -0.5, 0.0, 0.0, 0.5, 0.0, 0.0, 0.0, 1.0, 0.0)
    uvs = struct.pack("<6f", 0.0, 0.0, 1.0, 0.0, 0.5, 1.0)
    colors = struct.pack("<12f", 1.0, 0.0, 0.0, 1.0, 1.0, 0.0, 0.0, 1.0, 1.0, 0.0, 0.0, 1.0)
    indices = struct.pack("<3H", 0, 1, 2)
    blob = positions + uvs + indices + colors
    image_offset = len(blob)
    blob += image_bytes
    document = {
        "asset": {"version": "2.0", "generator": "static-player-smoke"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"name": "Player", "mesh": 0}],
        "meshes": [{"name": "Player", "primitives": [{"attributes": {"POSITION": 0, "TEXCOORD_0": 1, "COLOR_0": 3}, "indices": 2, "material": 0}]}],
        "materials": [{"name": "Player", "pbrMetallicRoughness": {"baseColorTexture": {"index": 0}}}],
        "textures": [{"source": 0}],
        "images": [{"bufferView": 4, "mimeType": "image/png", "name": "player"}],
        "bufferViews": [
            {"buffer": 0, "byteOffset": 0, "byteLength": len(positions)},
            {"buffer": 0, "byteOffset": len(positions), "byteLength": len(uvs)},
            {"buffer": 0, "byteOffset": len(positions) + len(uvs), "byteLength": len(indices)},
            {"buffer": 0, "byteOffset": len(positions) + len(uvs) + len(indices), "byteLength": len(colors)},
            {"buffer": 0, "byteOffset": image_offset, "byteLength": len(image_bytes)},
        ],
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [-0.5, 0.0, 0.0], "max": [0.5, 1.0, 0.0]},
            {"bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC2", "min": [0.0, 0.0], "max": [1.0, 1.0]},
            {"bufferView": 2, "componentType": 5123, "count": 3, "type": "SCALAR", "min": [0], "max": [2]},
            {"bufferView": 3, "componentType": 5126, "count": 3, "type": "VEC4"},
        ],
        "buffers": [{"byteLength": len(blob)}],
    }
    raw = json.dumps(document, separators=(",", ":"), sort_keys=True).encode("utf-8")
    raw += b" " * ((-len(raw)) % 4)
    binary = blob + b"\0" * ((-len(blob)) % 4)
    payload = bytearray(b"glTF")
    payload.extend(struct.pack("<II", 2, 12 + 8 + len(raw) + 8 + len(binary)))
    payload.extend(struct.pack("<II", len(raw), 0x4E4F534A))
    payload.extend(raw)
    payload.extend(struct.pack("<II", len(binary), 0x004E4942))
    payload.extend(binary)
    path.write_bytes(payload)


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="static-player-smoke-") as temp:
        root = Path(temp)
        source = root / "fixture.glb"
        recipe = root / "recipe.json"
        _fixture(source)
        recipe.write_text(json.dumps({
            "texture_image_index": 0,
            "uv_scale": [0.5, 0.5],
            "base_scale": 5.0,
            "axis_transform": "game-y-up-xz-travel",
            "pivot": "origin",
        }, sort_keys=True))
        outputs = []
        for index in (1, 2):
            out_dir = root / f"out-{index}"
            command = [sys.executable, str(ROOT / "tools/prepare_n64_model.py"), "--static-player", "--source", str(source), "--asset-id", "player", "--recipe-json", str(recipe), "--out-dir", str(out_dir)]
            subprocess.run(command, cwd=ROOT, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            report = json.loads((out_dir / "player.report.json").read_text())
            assert report["skin_present"] is False and report["animation_count"] == 0
            assert report["mesh_count"] == 1 and report["triangle_count"] == 1
            assert report["flat_shading"] is True and report["material_names"] == ["Player"]
            assert report["bounds"]["min"] == [-0.5, 0.0, 0.0]
            assert report["bounds"]["max"] == [0.5, 1.0, 0.0]
            assert report["image_dependencies"] == []
            assert report["vertex_color_attribute"] == "COLOR_0"
            assert report["alpha_policy"] == "opaque"
            assert len(report["color_source_sha256"]) == 64
            prepared, prepared_binary = read_glb(out_dir / "player.glb")
            assert not prepared.get("images") and not prepared.get("textures")
            primitive = prepared["meshes"][0]["primitives"][0]
            assert "TEXCOORD_0" not in primitive["attributes"]
            color_accessor = prepared["accessors"][primitive["attributes"]["COLOR_0"]]
            color_view = prepared["bufferViews"][color_accessor["bufferView"]]
            color_start = color_view.get("byteOffset", 0) + color_accessor.get("byteOffset", 0)
            first_color = struct.unpack_from("<4f", prepared_binary, color_start)
            assert first_color == (1.0, 0.0, 0.0, 1.0), f"known triangle color changed: {first_color}"
            outputs.append(((out_dir / "player.glb").read_bytes(), report))
        assert outputs[0][0] == outputs[1][0], "static preparation is not deterministic"
        assert outputs[0][1]["prepared_sha256"] == outputs[1][1]["prepared_sha256"]
    print("static player preparation smoke: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
