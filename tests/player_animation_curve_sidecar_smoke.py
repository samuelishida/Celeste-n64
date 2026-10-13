#!/usr/bin/env python3
"""Check curve-sidecar serialization against every prepared GLB key."""

from pathlib import Path
import json
import math
import struct
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

from build_player_animation_sidecar import (  # noqa: E402
    CLIPS, HEADER, PATHS, TRACK, build_sidecar,
)
from build_player_skin_sidecar import _read_accessor  # noqa: E402
from generate_player_animation_source import source_metadata  # noqa: E402
from glb_assets import read_glb  # noqa: E402


def _slerp(a: tuple[float, ...], b: tuple[float, ...], amount: float) -> tuple[float, ...]:
    dot = sum(x * y for x, y in zip(a, b))
    sign = -1.0 if dot < 0.0 else 1.0
    dot = min(abs(dot), 1.0)
    left, right = 1.0 - amount, amount * sign
    if dot < 1.0 - 1e-6:
        angle = math.acos(dot)
        inverse_sine = 1.0 / math.sin(angle)
        left = math.sin((1.0 - amount) * angle) * inverse_sine
        right = math.sin(amount * angle) * inverse_sine * sign
    return tuple(x * left + y * right for x, y in zip(a, b))


def _sample(keys: list[tuple[float, tuple[float, ...]]], time: float,
            interpolation: str) -> tuple[float, ...]:
    if time <= keys[0][0]:
        return keys[0][1]
    left = 0
    while left + 1 < len(keys) and keys[left + 1][0] <= time:
        left += 1
    if left + 1 == len(keys) or interpolation == "STEP":
        return keys[left][1]
    start, a = keys[left]
    end, b = keys[left + 1]
    amount = (time - start) / (end - start)
    return _slerp(a, b, amount) if len(a) == 4 else tuple(
        x + (y - x) * amount for x, y in zip(a, b)
    )


def _tracks(document: dict, binary: bytes, animation: dict) -> dict:
    result = {}
    for channel in animation["channels"]:
        sampler = animation["samplers"][channel["sampler"]]
        bone = document["nodes"][channel["target"]["node"]]["name"]
        path = channel["target"]["path"]
        times = _read_accessor(document, binary, sampler["input"])
        values = _read_accessor(document, binary, sampler["output"])
        result[(bone, path)] = (
            sampler.get("interpolation", "LINEAR"),
            list(zip((row[0] for row in times), values)),
        )
    return result


def main() -> int:
    glb_path = ROOT / "build/n64-prepared/player.glb"
    t3dm_path = ROOT / "assets/og_converted/models/player.t3dm"
    source_path = ROOT / "Celeste64-og/Content/Models/player.glb"
    source_document, source_binary = read_glb(source_path)
    prepared_document, prepared_binary = read_glb(glb_path)
    _, source_scale_tracks = source_metadata(source_path)
    source_scale_overrides = {(track["clip"], track["bone"]) for track in source_scale_tracks}
    report_data = json.loads((ROOT / "build/n64-prepared/player.report.json").read_text())
    root_offsets = {}
    normalized = report_data["normalized_skin_root"]
    for root, skin_joint in zip(normalized.get("roots", []), normalized.get("compensated_joint_roots", [])):
        node_index = skin_joint
        root_offsets[source_document["nodes"][node_index]["name"]] = tuple(root["offset"])
    source_animations = {item["name"]: item for item in source_document["animations"]}
    prepared_animations = {item["name"]: item for item in prepared_document["animations"]}
    max_source_prep_error = 0.0
    for clip in CLIPS:
        source_tracks = _tracks(source_document, source_binary, source_animations[clip])
        prepared_tracks = _tracks(prepared_document, prepared_binary, prepared_animations[clip])
        assert set(source_tracks) == set(prepared_tracks), clip
        duration = max(keys[-1][0] for _, keys in source_tracks.values())
        times_to_check = {tick / 240.0 for tick in range(math.ceil(duration * 240) + 1)
                          if tick / 240.0 <= duration}
        for key in source_tracks:
            source_interp, source_keys = source_tracks[key]
            prepared_interp, prepared_keys = prepared_tracks[key]
            times_to_check.update(time for time, _ in source_keys)
            times_to_check.update(time for time, _ in prepared_keys)
        for time in times_to_check:
            for bone, path in source_tracks:
                source_interp, source_keys = source_tracks[(bone, path)]
                prepared_interp, prepared_keys = prepared_tracks[(bone, path)]
                source_value = _sample(source_keys, time, source_interp)
                prepared_value = _sample(prepared_keys, time, prepared_interp)
                if path == "scale" and (clip, bone) in source_scale_overrides:
                    prepared_value = source_value  # runtime restores varying source STEP scale
                offset = root_offsets.get(bone, (0.0, 0.0, 0.0)) if path == "translation" else ()
                for component, (actual, expected) in enumerate(zip(source_value, prepared_value)):
                    adjusted_source = actual + (offset[component] if offset else 0.0)
                    max_source_prep_error = max(max_source_prep_error, abs(adjusted_source - expected))
    assert max_source_prep_error <= 5e-6, max_source_prep_error

    checked = 0
    with tempfile.TemporaryDirectory(prefix="player-curves-") as temp_dir:
        output = Path(temp_dir) / "player.anim"
        report = build_sidecar(glb_path, t3dm_path, output)
        data = output.read_bytes()
        assert data == (ROOT / "assets/og_converted/models/player.anim").read_bytes(), \
            "checked-in/generated animation sidecar is stale"

    assert report == {
        "clips": 7, "joints": 17, "tracks": 357, "keys": 3645,
        "float_values": 13220, "bytes": len(data), "base_scale": 64.0,
    }, report
    magic, version, clips, joints, tracks, base_scale = HEADER.unpack_from(data)
    assert (magic, version, clips, joints, tracks) == (b"PAS1", 1, 7, 17, 357)
    assert base_scale == 64.0

    document, binary = read_glb(glb_path)
    animations = {animation["name"]: animation for animation in document["animations"]}
    joint_names = [document["nodes"][index]["name"] for index in document["skins"][0]["joints"]]
    cursor = HEADER.size
    for clip_index, clip in enumerate(CLIPS):
        animation = animations[clip]
        expected = {}
        for channel in animation["channels"]:
            sampler = animation["samplers"][channel["sampler"]]
            node = document["nodes"][channel["target"]["node"]]
            key = (joint_names.index(node["name"]), PATHS[channel["target"]["path"]])
            expected[key] = (
                sampler.get("interpolation", "LINEAR"),
                _read_accessor(document, binary, sampler["input"]),
                _read_accessor(document, binary, sampler["output"]),
            )
        for _ in range(51):
            clip_id, bone, path, interpolation, key_count, reserved = TRACK.unpack_from(data, cursor)
            cursor += TRACK.size
            assert clip_id == clip_index and reserved == 0
            identity = (bone, path)
            assert identity in expected, (clip, identity)
            interpolation_name = "LINEAR" if interpolation == 0 else "STEP"
            expected_interpolation, times, values = expected.pop(identity)
            assert interpolation_name == expected_interpolation
            assert key_count == len(times) == len(values)
            components = 4 if path == PATHS["rotation"] else 3
            for time, value in zip(times, values):
                actual_time = struct.unpack_from(">f", data, cursor)[0]
                cursor += 4
                actual_value = struct.unpack_from(">" + "f" * components, data, cursor)
                cursor += 4 * components
                assert actual_time == time[0]
                assert actual_value == value
                checked += 1
        assert not expected, f"{clip}: missing curves {sorted(expected)}"
    assert cursor == len(data)
    print(f"player_animation_curve_sidecar_smoke: PASS ({tracks} curves, {checked} keys, "
          f"{len(data)} bytes, OG/prepared max error {max_source_prep_error:.2g})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
