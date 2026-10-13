#!/usr/bin/env python3
"""Preserve exact prepared-GLB player curves for runtime pose sampling."""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
import struct

from build_player_skin_sidecar import _read_accessor
from glb_assets import read_glb


HEADER = struct.Struct(">4sHHHHf")
TRACK = struct.Struct(">BBBBHH")
PATHS = {"translation": 0, "rotation": 1, "scale": 2}
INTERPOLATIONS = {"LINEAR": 0, "STEP": 1}
CLIPS = ("Idle", "Run", "Jump", "Dash", "Climb.Idle", "Climb.Up", "Skid")


def _string(data: bytes, offset: int) -> str:
    end = data.find(b"\0", offset)
    if offset < 0 or offset >= len(data) or end < 0:
        raise ValueError(f"invalid T3DM string offset {offset}")
    return data[offset:end].decode("utf-8")


def t3dm_joint_names(path: Path) -> list[str]:
    data = path.read_bytes()
    if len(data) < 48 or data[:4] != b"T3M\x04":
        raise ValueError(f"{path}: invalid T3DM")
    chunks = struct.unpack_from(">I", data, 4)[0]
    string_table = struct.unpack_from(">I", data, 24)[0]
    if 44 + chunks * 4 > len(data) or string_table >= len(data):
        raise ValueError(f"{path}: malformed T3DM chunk table")
    for index in range(chunks):
        word = struct.unpack_from(">I", data, 44 + index * 4)[0]
        if word >> 24 != ord("S"):
            continue
        offset = word & 0x00FFFFFF
        count = struct.unpack_from(">H", data, offset)[0]
        if offset + 4 + count * 48 > len(data):
            raise ValueError("truncated T3DM skeleton")
        names = []
        for bone in range(count):
            name_offset = struct.unpack_from(">I", data, offset + 4 + bone * 48)[0]
            names.append(_string(data, string_table + name_offset))
        if len(names) != len(set(names)):
            raise ValueError("T3DM skeleton has ambiguous bone names")
        return names
    raise ValueError("T3DM skeleton chunk missing")


def build_sidecar(glb_path: Path, t3dm_path: Path, output_path: Path,
                  base_scale: float = 64.0) -> dict[str, int | float]:
    if not math.isfinite(base_scale) or base_scale <= 0:
        raise ValueError(f"base scale must be finite and positive: {base_scale}")
    document, binary = read_glb(glb_path)
    joints = t3dm_joint_names(t3dm_path)
    if not document.get("skins") or not document["skins"][0].get("joints"):
        raise ValueError("prepared player GLB has no skin joints")
    glb_joints = [document["nodes"][index].get("name") for index in document["skins"][0]["joints"]]
    if glb_joints != joints:
        raise ValueError("prepared GLB/T3DM joint order differs")
    joint_indices = {name: index for index, name in enumerate(joints)}
    animations = {item.get("name"): item for item in document.get("animations", [])}
    if not set(CLIPS).issubset(animations):
        raise ValueError("prepared GLB is missing one or more required player clips")

    payload = bytearray(HEADER.pack(b"PAS1", 1, len(CLIPS), len(joints), 0, base_scale))
    track_count = 0
    key_count = 0
    float_count = 0
    for clip_index, clip_name in enumerate(CLIPS):
        animation = animations[clip_name]
        seen: set[tuple[int, int]] = set()
        for channel in animation.get("channels", []):
            target = channel.get("target", {})
            path = target.get("path")
            if path not in PATHS:
                raise ValueError(f"{clip_name}: unsupported animation path {path!r}")
            node_index = target.get("node")
            if not isinstance(node_index, int) or not 0 <= node_index < len(document["nodes"]):
                raise ValueError(f"{clip_name}: invalid animation node")
            bone_name = document["nodes"][node_index].get("name")
            if bone_name not in joint_indices:
                raise ValueError(f"{clip_name}: animated node is not a skin joint: {bone_name}")
            bone_index = joint_indices[bone_name]
            path_code = PATHS[path]
            identity = (bone_index, path_code)
            if identity in seen:
                raise ValueError(f"{clip_name}: duplicate animation target {bone_name}/{path}")
            seen.add(identity)
            sampler_index = channel.get("sampler")
            samplers = animation.get("samplers", [])
            if not isinstance(sampler_index, int) or not 0 <= sampler_index < len(samplers):
                raise ValueError(f"{clip_name}: invalid sampler index")
            sampler = samplers[sampler_index]
            interpolation = sampler.get("interpolation", "LINEAR")
            if interpolation not in INTERPOLATIONS:
                raise ValueError(f"{clip_name}: unsupported interpolation {interpolation}")
            times = _read_accessor(document, binary, sampler.get("input"))
            values = _read_accessor(document, binary, sampler.get("output"))
            expected_width = 4 if path == "rotation" else 3
            if (not times or len(times) != len(values) or len(times) > 65535 or
                    any(len(value) != expected_width for value in values)):
                raise ValueError(f"{clip_name}/{bone_name}/{path}: malformed key array")
            key_times = [row[0] for row in times]
            if key_times[0] < 0 or any(left >= right for left, right in zip(key_times, key_times[1:])):
                raise ValueError(f"{clip_name}/{bone_name}/{path}: unordered key times")
            payload.extend(TRACK.pack(clip_index, bone_index, path_code,
                                      INTERPOLATIONS[interpolation], len(key_times), 0))
            for time, value in zip(key_times, values):
                payload.extend(struct.pack(">f", time))
                payload.extend(struct.pack(">" + "f" * expected_width, *value))
            track_count += 1
            key_count += len(key_times)
            float_count += sum(map(len, values))
        if not seen:
            raise ValueError(f"{clip_name}: no animation curves")
    payload[:HEADER.size] = HEADER.pack(b"PAS1", 1, len(CLIPS), len(joints), track_count, base_scale)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_bytes(payload)
    return {
        "clips": len(CLIPS),
        "joints": len(joints),
        "tracks": track_count,
        "keys": key_count,
        "float_values": float_count,
        "bytes": len(payload),
        "base_scale": base_scale,
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--glb", type=Path, required=True)
    parser.add_argument("--t3dm", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--base-scale", type=float, default=64.0)
    args = parser.parse_args()
    print(json.dumps(build_sidecar(args.glb, args.t3dm, args.out, args.base_scale), sort_keys=True))


if __name__ == "__main__":
    main()
