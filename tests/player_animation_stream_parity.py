#!/usr/bin/env python3
"""Diagnostic: quantify Tiny3D key-time quantization against prepared GLB."""

from __future__ import annotations

import math
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

from build_player_skin_sidecar import _read_accessor  # noqa: E402
from generate_player_animation_source import source_metadata  # noqa: E402
from glb_assets import read_glb  # noqa: E402


CLIPS = ("Idle", "Run", "Jump", "Dash", "Climb.Idle", "Climb.Up", "Skid")
BASE_SCALE = 64.0
SQRT_2_INV = 1.0 / math.sqrt(2.0)


def _cstring(data: bytes, offset: int) -> str:
    if not 0 <= offset < len(data):
        raise ValueError(f"string offset outside T3DM: {offset}")
    end = data.find(b"\0", offset)
    if end < 0:
        raise ValueError(f"unterminated T3DM string at {offset}")
    return data[offset:end].decode("utf-8")


def _model_chunks(path: Path) -> tuple[dict[str, dict], list[str], dict[str, tuple]]:
    data = path.read_bytes()
    if data[:4] != b"T3M\x04":
        raise ValueError(f"unsupported T3DM header: {path}")
    chunk_count = struct.unpack_from(">I", data, 4)[0]
    string_table = struct.unpack_from(">I", data, 24)[0]
    if 44 + chunk_count * 4 > len(data) or string_table >= len(data):
        raise ValueError(f"invalid T3DM chunk table: {path}")
    chunks: dict[str, int] = {}
    skeleton: list[str] = []
    rest: dict[str, tuple] = {}
    animations: dict[str, dict] = {}
    for index in range(chunk_count):
        word = struct.unpack_from(">I", data, 44 + index * 4)[0]
        kind, offset = word >> 24, word & 0x00FFFFFF
        if offset >= len(data):
            raise ValueError(f"T3DM chunk outside file: {index}")
        if kind == ord("S"):
            count = struct.unpack_from(">H", data, offset)[0]
            if offset + 4 + count * 48 > len(data):
                raise ValueError("truncated T3DM skeleton")
            for bone in range(count):
                record = offset + 4 + bone * 48
                name_offset = struct.unpack_from(">I", data, record)[0]
                name = _cstring(data, string_table + name_offset)
                skeleton.append(name)
                scale = struct.unpack_from(">3f", data, record + 8)
                rotation = struct.unpack_from(">4f", data, record + 20)
                position = struct.unpack_from(">3f", data, record + 36)
                rest[name] = (scale, rotation, position)
        elif kind == ord("A"):
            name_off, duration, key_count, quat_count, scalar_count, stream_off = struct.unpack_from(
                ">IfIHHI", data, offset
            )
            name = _cstring(data, string_table + name_off)
            stream = _cstring(data, string_table + stream_off)
            channels = []
            for channel in range(quat_count + scalar_count):
                target, target_type, axis, quant_scale, quant_offset = struct.unpack_from(
                    ">HBBff", data, offset + 20 + channel * 12
                )
                if target >= len(skeleton) and skeleton:
                    raise ValueError(f"{name}: animation target index {target} outside skeleton")
                channels.append({
                    "bone": target,
                    "type": target_type,
                    "axis": axis,
                    "quant_scale": quant_scale,
                    "quant_offset": quant_offset,
                })
            animations[name] = {
                "duration": duration,
                "key_count": key_count,
                "quat_count": quat_count,
                "scalar_count": scalar_count,
                "stream": stream,
                "channels": channels,
            }
            chunks[name] = offset
    if not skeleton or not set(CLIPS).issubset(animations):
        raise ValueError(f"T3DM rig/clips mismatch: bones={len(skeleton)}, clips={sorted(animations)}")
    return animations, skeleton, rest


def _unpack_quat(hi: int, lo: int) -> tuple[float, float, float, float]:
    largest = hi >> 14
    middle = ((hi << 6) | (lo >> 10)) & 0xFFFF

    def s10(value: int) -> float:
        return value / 1023.0 * (2.0 * SQRT_2_INV) - SQRT_2_INV

    values = [0.0] * 4
    values[(largest + 1) & 3] = s10((hi >> 4) & 0x3FF)
    values[(largest + 2) & 3] = s10(middle & 0x3FF)
    values[(largest + 3) & 3] = s10(lo & 0x3FF)
    values[largest] = math.sqrt(max(0.0, 1.0 - sum(value * value for i, value in enumerate(values) if i != largest)))
    return tuple(values)


def _decode_stream(path: Path, definition: dict) -> list[list[tuple[float, tuple[float, ...]]]]:
    data = path.read_bytes()
    size = 8
    cursor = 0
    rows: list[list[tuple[float, tuple[float, ...]]]] = [[] for _ in definition["channels"]]
    ticks = [0] * len(rows)
    total = 0
    while cursor < len(data):
        if cursor + size > len(data):
            raise ValueError(f"truncated keyframe in {path} at {cursor}")
        packed, channel = struct.unpack_from(">HH", data, cursor)
        if channel >= len(rows):
            raise ValueError(f"{path}: channel index {channel} outside animation definition")
        payload = struct.unpack_from(">H", data, cursor + 4)[0]
        if channel < definition["quat_count"]:
            if size != 8:
                raise ValueError(f"{path}: quaternion record has scalar size")
            second = struct.unpack_from(">H", data, cursor + 6)[0]
            value = _unpack_quat(payload, second)
        else:
            mapping = definition["channels"][channel]
            value = (payload * mapping["quant_scale"] + mapping["quant_offset"],)
        delta = packed & 0x7FFF
        ticks[channel] += delta
        rows[channel].append((ticks[channel] / 60.0, value))
        current_size = size
        size = 8 if packed & 0x8000 else 6
        cursor += current_size
        total += 1
    if total != definition["key_count"]:
        raise ValueError(f"{path}: decoded {total} records, expected {definition['key_count']}")
    return rows


def _slerp(a: tuple[float, ...], b: tuple[float, ...], amount: float) -> tuple[float, ...]:
    dot = sum(x * y for x, y in zip(a, b))
    sign = -1.0 if dot < 0 else 1.0
    dot = min(abs(dot), 1.0)
    left, right = 1.0 - amount, amount * sign
    if dot < 1.0 - 1.0e-6:
        angle = math.acos(dot)
        inverse_sine = 1.0 / math.sin(angle)
        left = math.sin((1.0 - amount) * angle) * inverse_sine
        right = math.sin(amount * angle) * inverse_sine * sign
    return tuple(x * left + y * right for x, y in zip(a, b))


def _sample(keys: list[tuple[float, tuple[float, ...]]], time: float, interpolation: str) -> tuple[float, ...]:
    if time <= keys[0][0]:
        return keys[0][1]
    index = 0
    while index + 1 < len(keys) and time >= keys[index + 1][0]:
        index += 1
    if index + 1 == len(keys):
        return keys[index][1]
    left_time, left = keys[index]
    right_time, right = keys[index + 1]
    if interpolation == "STEP" or right_time <= left_time:
        return left
    amount = (time - left_time) / (right_time - left_time)
    if len(left) == 4:
        return _slerp(left, right, amount)
    return tuple(a + (b - a) * amount for a, b in zip(left, right))


def _angle_error(a: tuple[float, ...], b: tuple[float, ...]) -> float:
    length_a = math.sqrt(sum(value * value for value in a))
    length_b = math.sqrt(sum(value * value for value in b))
    if length_a == 0 or length_b == 0:
        return 180.0
    dot = abs(sum(x * y for x, y in zip(a, b))) / (length_a * length_b)
    return math.degrees(2.0 * math.acos(min(1.0, dot)))


def _source_step_scale(clip: str, bone: str, time: float, tracks: list[dict]) -> tuple[float, ...] | None:
    for track in tracks:
        if track["clip"] != clip or track["bone"] != bone:
            continue
        key = 0
        while key + 1 < len(track["times"]) and track["times"][key + 1] <= time:
            key += 1
        return tuple(track["values"][key])
    return None


def main() -> int:
    prepared_path = ROOT / "build/n64-prepared/player.glb"
    model_path = ROOT / "assets/og_converted/models/player.t3dm"
    document, binary = read_glb(prepared_path)
    animations, skeleton, rest = _model_chunks(model_path)
    source_doc, source_binary = read_glb(ROOT / "Celeste64-og/Content/Models/player.glb")
    _, source_scale_tracks = source_metadata(ROOT / "Celeste64-og/Content/Models/player.glb")
    source_by_name = {item["name"]: item for item in source_doc["animations"]}
    prepared_by_name = {item["name"]: item for item in document["animations"]}
    animmap = (ROOT / "assets/og_converted/models/player.animmap").read_text().splitlines()[1:]
    stream_by_clip = {fields[0]: ROOT / "assets/og_converted/models" / Path(fields[1]).name
                      for fields in (line.split("\t") for line in animmap)}

    gltf_tracks: dict[str, dict[tuple[str, str], dict]] = {}
    expected_channels: dict[str, set[tuple[str, str, int]]] = {}
    for clip in CLIPS:
        animation = prepared_by_name[clip]
        tracks: dict[tuple[str, str], dict] = {}
        expanded: set[tuple[str, str, int]] = set()
        for channel in animation["channels"]:
            node = document["nodes"][channel["target"]["node"]]
            bone = node.get("name")
            path = channel["target"]["path"]
            if bone not in skeleton or path not in ("translation", "rotation", "scale"):
                continue
            sampler = animation["samplers"][channel["sampler"]]
            interpolation = sampler.get("interpolation", "LINEAR")
            times = [row[0] for row in _read_accessor(document, binary, sampler["input"])]
            values = _read_accessor(document, binary, sampler["output"])
            if len(times) != len(values) or any(a >= b for a, b in zip(times, times[1:])):
                raise ValueError(f"{clip}/{bone}/{path}: invalid prepared sampler")
            keys = list(zip(times, values))
            tracks[(bone, path)] = {"keys": keys, "interpolation": interpolation}
            if path == "rotation":
                expanded.add((bone, path, -1))
            else:
                expanded.update((bone, path, axis) for axis in range(3))
        gltf_tracks[clip] = tracks
        expected_channels[clip] = expanded

    maxima = {"translation": (0.0, None), "scale": (0.0, None), "rotation_deg": (0.0, None)}
    clip_maxima = {clip: {metric: 0.0 for metric in maxima} for clip in CLIPS}
    checked = 0
    for clip in CLIPS:
        definition = animations[clip]
        if clip not in stream_by_clip:
            raise ValueError(f"animmap missing {clip}")
        records = _decode_stream(stream_by_clip[clip], definition)
        actual_channels: set[tuple[str, str, int]] = set()
        for index, mapping in enumerate(definition["channels"]):
            bone = skeleton[mapping["bone"]]
            if mapping["type"] == 3:
                key = (bone, "rotation", -1)
            elif mapping["type"] in (0, 1) and mapping["axis"] < 3:
                key = (bone, "translation" if mapping["type"] == 0 else "scale", mapping["axis"])
            else:
                raise ValueError(f"{clip}: unsupported Tiny3D mapping {mapping}")
            if key in actual_channels:
                raise ValueError(f"{clip}: duplicate Tiny3D channel {key}")
            actual_channels.add(key)
            gltf = gltf_tracks[clip].get((bone, key[1]))
            if gltf is None:
                raise ValueError(f"{clip}: Tiny3D channel lacks prepared GLB track: {key}")
            source_clip = source_by_name[clip]
            source_tracks = {}
            for channel in source_clip["channels"]:
                source_sampler = source_clip["samplers"][channel["sampler"]]
                if channel["target"]["path"] != "scale" or source_sampler.get("interpolation") != "STEP":
                    continue
                source_bone = source_doc["nodes"][channel["target"]["node"]].get("name")
                if source_bone != bone:
                    continue
                source_times = [row[0] for row in _read_accessor(source_doc, source_binary, source_sampler["input"])]
                source_values = _read_accessor(source_doc, source_binary, source_sampler["output"])
                if any(value != source_values[0] for value in source_values):
                    source_tracks[bone] = {"keys": list(zip(source_times, source_values)), "interpolation": "STEP"}

            # State rate can vary continuously, so clip time is not limited to
            # 60 Hz even though simulation ticks are. Probe a denser grid plus
            # every GLB/stream key and each interval midpoint.
            duration = definition["duration"]
            sample_times = {tick / 240.0 for tick in range(math.ceil(duration * 240) + 1)
                            if tick / 240.0 <= duration + 1e-7}
            for time, _ in gltf["keys"] + records[index]:
                sample_times.add(time)
            ordered_keys = sorted({time for time, _ in gltf["keys"]} |
                                  {time for time, _ in records[index]})
            sample_times.update((left + right) * 0.5 for left, right in zip(ordered_keys, ordered_keys[1:])
                                if right > left)
            for time in sorted(sample_times):
                sample_time = time % duration if time >= duration else time
                expected = _sample(gltf["keys"], sample_time, gltf["interpolation"])
                actual = _sample(records[index], sample_time, "LINEAR")
                if key[1] == "rotation":
                    error = _angle_error(expected, actual)
                    metric = "rotation_deg"
                else:
                    expected_component = expected[key[2]] * (BASE_SCALE if key[1] == "translation" else 1.0)
                    actual_component = actual[0]
                    if key[1] == "scale" and bone in source_tracks:
                        source_expected = _sample(source_tracks[bone]["keys"], sample_time, "STEP")
                        expected_component = source_expected[key[2]]
                    error = abs(expected_component - actual_component)
                    metric = key[1]
                checked += 1
                clip_maxima[clip][metric] = max(clip_maxima[clip][metric], error)
                if error > maxima[metric][0]:
                    nearby = lambda keys: sorted(keys, key=lambda item: abs(item[0] - sample_time))[:4]
                    maxima[metric] = (error, (clip, bone, key[1], key[2], time,
                                               expected, actual, gltf["interpolation"],
                                               nearby(gltf["keys"]), nearby(records[index])))
        missing = expected_channels[clip] - actual_channels
        for bone, path, axis in sorted(missing):
            gltf = gltf_tracks[clip][(bone, path)]
            values = [value for _, value in gltf["keys"]]
            rest_values = rest[bone][{"scale": 0, "rotation": 1, "translation": 2}[path]]
            component_values = [value if path == "rotation" else (value[axis],) for value in values]
            component_rest = rest_values if path == "rotation" else (rest_values[axis],)
            if path == "translation":
                component_rest = tuple(value / BASE_SCALE for value in component_rest)
            if any(max(abs(a - b) for a, b in zip(value, component_values[0])) > 1e-5
                   for value in component_values[1:]):
                raise ValueError(f"{clip}: moving GLB channel omitted by Tiny3D: {(bone, path, axis)}")
            if path == "rotation":
                rest_matches = _angle_error(component_values[0], component_rest) <= 0.02
            else:
                rest_matches = abs(component_values[0][0] - component_rest[0]) <= 1e-5
            if not rest_matches:
                raise ValueError(f"{clip}: constant GLB channel differs from Tiny3D rest pose: {(bone, path, axis)}")

    for metric, (error, context) in maxima.items():
        print(f"{metric}: max={error:.8g} at {context}")
    for clip in CLIPS:
        print(f"{clip}: t={clip_maxima[clip]['translation']:.6g} "
              f"s={clip_maxima[clip]['scale']:.6g} "
              f"r={clip_maxima[clip]['rotation_deg']:.6g}deg")
    print(f"Tiny3D stream diagnostic only ({checked} channel samples); exact curve sidecar is runtime authority")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
