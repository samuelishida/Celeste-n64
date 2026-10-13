#!/usr/bin/env python3
"""Inspect the animated texture-free player T3DM and its runtime streams."""

from __future__ import annotations

import json
import math
import re
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main() -> int:
    manifest = json.loads((ROOT / "assets/og_converted/asset_manifest.json").read_text())
    player = next(asset for asset in manifest["assets"] if asset["id"] == "player")
    assert player["profile"] == "animated_model"
    prepared = json.loads((ROOT / "build/n64-prepared/player.report.json").read_text())
    assert prepared["animated"] and prepared["joint_count"] == 17
    assert prepared["image_dependencies"] == [] and prepared["color_samples"] > 0
    assert set(("Idle", "Run", "Jump", "Dash", "Skid", "Climb.Idle", "Climb.Up")) <= set(prepared["clip_names"])

    document_path = ROOT / "build/n64-prepared/player.glb"
    raw = document_path.read_bytes()
    json_length = struct.unpack_from("<I", raw, 12)[0]
    document = json.loads(raw[20:20 + json_length].rstrip(b" \t\r\n\0"))
    binary = raw[20 + json_length + 8:]
    assert document.get("skins") and document.get("animations")
    assert not document.get("images") and not document.get("textures")
    for mesh in document["meshes"]:
        for primitive in mesh["primitives"]:
            attributes = primitive["attributes"]
            assert {"POSITION", "COLOR_0", "JOINTS_0", "WEIGHTS_0"} <= set(attributes)
            assert "TEXCOORD_0" in attributes, "animated vertices need stable packed skin IDs"
            uv_accessor = document["accessors"][attributes["TEXCOORD_0"]]
            assert uv_accessor["componentType"] == 5126 and uv_accessor["type"] == "VEC2"
            assert uv_accessor["count"] == document["accessors"][attributes["POSITION"]]["count"]
            assert uv_accessor["extras"]["n64SkinSourceVertexCount"] == uv_accessor["count"]

    path = ROOT / "assets/og_converted/models/player.t3dm"
    data = path.read_bytes()
    assert data[:4] == b"T3M\x04"
    chunk_count = struct.unpack_from(">I", data, 4)[0]
    vertex_count = struct.unpack_from(">H", data, 8)[0]
    string_table = struct.unpack_from(">I", data, 24)[0]
    materials = 0
    chunk_types = []
    vertex_offsets = []
    streams = []
    for index in range(chunk_count):
        word = struct.unpack_from(">I", data, 44 + index * 4)[0]
        chunk_type, offset = word >> 24, word & 0x00FFFFFF
        chunk_types.append(chunk_type)
        if chunk_type == ord("V"):
            vertex_offsets.append(offset)
        elif chunk_type == ord("A"):
            name_offset = struct.unpack_from(">I", data, offset)[0]
            path_offset = struct.unpack_from(">I", data, offset + 16)[0]
            name_start = string_table + name_offset
            path_start = string_table + path_offset
            name_end = data.index(0, name_start)
            path_end = data.index(0, path_start)
            streams.append((data[name_start:name_end].decode(), data[path_start:path_end].decode()))
        elif chunk_type == ord("M"):
            materials += 1
            assert offset + 140 <= len(data)
            flags = struct.unpack_from(">I", data, offset + 28)[0]
            assert flags & 0x7 == 0x5, f"material should be shaded/depth-only: 0x{flags:x}"
            assert struct.unpack_from(">Q", data, offset)[0] == 0x0000000000020904
            assert struct.unpack_from(">II", data, offset + 52) == (0, 0)
            assert struct.unpack_from(">HH", data, offset + 68) == (0, 0)

    assert materials >= 2
    assert ord("S") in chunk_types and len(streams) >= 7
    assert len({name for name, _ in streams}) == len(streams)
    assert path.stat().st_size <= 128 * 1024
    assert 0 < vertex_count <= 3 * 4096 and len(vertex_offsets) == 1
    vertex_offset = vertex_offsets[0]
    source_vertex_count = prepared["skin_source_vertex_ids"]["source_vertex_count"]
    packed_source_ids = [
        struct.unpack_from(">h", data, vertex_offset + (index // 2) * 32 + (24 if index % 2 == 0 else 28))[0]
        for index in range(vertex_count)
    ]
    assert all(0 <= source_id < source_vertex_count for source_id in packed_source_ids), \
        "T3DM conversion corrupted encoded source vertex IDs"
    assert set(packed_source_ids) == set(range(source_vertex_count)), \
        "T3DM output lost one or more source vertex IDs"
    color_end = vertex_offset + ((vertex_count - 1) // 2) * 32 + 20
    assert color_end <= len(data), "T3DM vertex/color payload is truncated"
    colors = [
        data[vertex_offset + (index // 2) * 32 + 16 + (index % 2) * 4:
             vertex_offset + (index // 2) * 32 + 20 + (index % 2) * 4]
        for index in range(vertex_count)
    ]
    unique_colors = set(colors)
    assert len(unique_colors) >= 6
    assert all(color[3] == 255 for color in unique_colors)
    assert any(color[0] > color[1] * 1.5 and color[0] > color[2] * 1.5 for color in colors), \
        "baked hair colors should contain red-orange values"

    animmap = (ROOT / "assets/og_converted/models/player.animmap").read_text().splitlines()
    declared = int(animmap[0].split("\t")[2])
    assert declared == 7 and len(animmap) == declared + 1
    expected = {line.split("\t")[0]: line.split("\t")[1] for line in animmap[1:]}
    assert {name: stream for name, stream in streams if name in expected} == expected
    for output in player["outputs"]:
        if output["path"].endswith(".sdata"):
            assert (ROOT / output["path"]).is_file()

    scene = (ROOT / "src/user/gameplay/scene/gameplay_scene.cpp").read_text()
    scale = float(re.search(r"kMadelineScale\s*=\s*([0-9.]+)f", scene).group(1))
    pivot = float(re.search(r"kMadelineFeetPivotOffset\s*=\s*([0-9.]+)f", scene).group(1))
    positions = []
    for index in range(vertex_count):
        start = vertex_offset + (index // 2) * 32 + (0 if index % 2 == 0 else 8)
        positions.append(struct.unpack_from(">hhh", data, start))
    min_x, max_x = min(p[0] for p in positions), max(p[0] for p in positions)
    min_y, max_y = min(p[1] for p in positions), max(p[1] for p in positions)
    min_z, max_z = min(p[2] for p in positions), max(p[2] for p in positions)
    normalization = prepared["normalized_skin_root"]
    source_offset = next(root["offset"] for root in normalization["roots"]
                         if abs(root["offset"][0]) > 1e-7)
    base_scale = float(player["transform"]["base_scale"])
    effective_min_x = min_x + source_offset[0] * base_scale
    effective_max_x = max_x + source_offset[0] * base_scale
    radius = max(math.hypot(x, z) for x in (effective_min_x, effective_max_x)
                 for z in (min_z, max_z)) * scale
    height = (max_y - min_y) * scale
    clearance = (ROOT / "src/user/gameplay/player/player_model_clearance.hpp").read_text()
    model_radius = float(re.search(r"kMadelineModelRadius\s*=\s*([0-9.]+)f", clearance).group(1))
    model_height = float(re.search(r"kMadelineModelHeight\s*=\s*([0-9.]+)f", clearance).group(1))
    root_offset = float(re.search(
        r"kMadelineModelRootOffsetX\s*=\s*([0-9.]+)f", clearance).group(1))
    assert abs(root_offset - source_offset[0] * base_scale * scale) < 1e-4
    assert abs(effective_min_x - float(re.search(
        r"kMadelineModelMinX\s*=\s*(-?[0-9.]+)f", clearance).group(1))) < 0.2
    assert abs(effective_max_x - float(re.search(
        r"kMadelineModelMaxX\s*=\s*(-?[0-9.]+)f", clearance).group(1))) < 0.2
    assert abs(min_z - float(re.search(
        r"kMadelineModelMinZ\s*=\s*(-?[0-9.]+)f", clearance).group(1))) < 0.2
    assert abs(max_z - float(re.search(
        r"kMadelineModelMaxZ\s*=\s*(-?[0-9.]+)f", clearance).group(1))) < 0.2
    assert 0 <= model_radius - radius < 0.2, "render clearance radius must conservatively cover effective mesh bounds"
    assert 0 <= model_height - height < 0.25, "render clearance height must conservatively cover packed mesh bounds"
    # Packed vertices are in bind-joint space, so their Y minimum is only a
    # coarse feet-pivot sanity check; the runtime skeleton restores bind pose.
    assert abs(min_y * scale + pivot) < 0.2, "packed model bounds drifted materially from feet pivot"
    print(f"animated player flat-color T3DM smoke: PASS ({materials} materials, {vertex_count} vertices, {len(unique_colors)} colors, {len(streams)} streams)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
