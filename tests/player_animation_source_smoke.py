#!/usr/bin/env python3
"""Source fidelity contract: generated metadata and delayed-first-key padding."""
import copy
from pathlib import Path
import struct
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from generate_player_animation_source import accessor_values, render_header, source_metadata
from glb_assets import read_glb, write_glb
from prepare_n64_model import _pad_animation_starts

source = ROOT / "Celeste64-og/Content/Models/player.glb"
durations, tracks = source_metadata(source)
assert abs(durations[6] - 0.8333333134651184) < 1e-8
assert len(tracks) == 10
assert {(track["clip"], track["bone"]) for track in tracks} == {
    ("Dash", "Hips"), ("Dash", "Backpack"), ("Dash", "Head"),
    ("Dash", "Arm.001.L"), ("Dash", "Arm.002.L"), ("Skid", "Arm.002.L"),
    ("Jump", "Head"), ("Jump", "Arm.001.L"), ("Jump", "Arm.001.R"), ("Jump", "Arm.002.R"),
}
header = ROOT / "src/user/gameplay/render/player_animation_source.hpp"
assert header.read_text(encoding="utf-8") == render_header(source), "Stale source animation metadata"

# A delayed LINEAR/STEP channel must clamp at the first value before time .25,
# retain subsequent authored keys and keep the original absolute end time.
binary = struct.pack("<8f", .25, 1.0, 1, 2, 3, 4, 5, 6)
document = {
    "asset": {"version": "2.0"}, "buffers": [{"byteLength": len(binary)}],
    "bufferViews": [{"buffer": 0, "byteOffset": 0, "byteLength": 8},
                    {"buffer": 0, "byteOffset": 8, "byteLength": 24}],
    "accessors": [{"bufferView": 0, "componentType": 5126, "count": 2,
                   "type": "SCALAR", "min": [.25], "max": [1.]},
                  {"bufferView": 1, "componentType": 5126, "count": 2, "type": "VEC3"}],
    "animations": [{"name": "delayed", "samplers": [
        {"input": 0, "output": 1, "interpolation": "LINEAR"},
        {"input": 0, "output": 1, "interpolation": "STEP"}], "channels": []}],
}
with tempfile.TemporaryDirectory() as temporary:
    path = Path(temporary) / "delayed.glb"
    write_glb(path, copy.deepcopy(document), binary)
    assert _pad_animation_starts(path) == ["delayed"]
    padded, padded_binary = read_glb(path)
    for sampler in padded["animations"][0]["samplers"]:
        assert accessor_values(padded, padded_binary, sampler["input"]) == [(0,), (.25,), (1,)]
        assert accessor_values(padded, padded_binary, sampler["output"]) == [(1, 2, 3), (1, 2, 3), (4, 5, 6)]
    once = path.read_bytes()
    assert _pad_animation_starts(path) == [] and path.read_bytes() == once

print("player_animation_source_smoke passed")
