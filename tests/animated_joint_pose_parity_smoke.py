#!/usr/bin/env python3
"""Compare source and runtime single-clip plus retained-track weighted poses."""

from __future__ import annotations

import math
from pathlib import Path
import subprocess
import struct
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

from build_player_skin_sidecar import _read_accessor  # noqa: E402
from generate_player_animation_source import source_metadata  # noqa: E402
from glb_assets import read_glb  # noqa: E402


CLIPS = ("Idle", "Run", "Jump", "Dash", "Climb.Idle", "Climb.Up", "Skid")
PATHS = {"translation": 0, "rotation": 1, "scale": 2}


def read_t3dm_skeleton(path: Path) -> list[dict]:
    data = path.read_bytes()
    if len(data) < 48 or data[:4] != b"T3M\x04":
        raise AssertionError("invalid player T3DM")
    chunk_count = struct.unpack_from(">I", data, 4)[0]
    string_table = struct.unpack_from(">I", data, 24)[0]
    skeleton_offset = None
    for index in range(chunk_count):
        word = struct.unpack_from(">I", data, 44 + index * 4)[0]
        if word >> 24 == ord("S"):
            skeleton_offset = word & 0x00FFFFFF
            break
    if skeleton_offset is None:
        raise AssertionError("T3DM skeleton chunk missing")
    count = struct.unpack_from(">H", data, skeleton_offset)[0]
    if not 0 < count <= 256 or skeleton_offset + 4 + count * 48 > len(data):
        raise AssertionError("malformed T3DM skeleton")
    bones = []
    for index in range(count):
        offset = skeleton_offset + 4 + index * 48
        name_offset = struct.unpack_from(">I", data, offset)[0]
        start = string_table + name_offset
        end = data.find(b"\0", start)
        if start >= len(data) or end < 0:
            raise AssertionError("invalid T3DM bone name")
        name = data[start:end].decode("utf-8")
        parent, depth = struct.unpack_from(">HH", data, offset + 4)
        scale = list(struct.unpack_from(">3f", data, offset + 8))
        rotation = list(struct.unpack_from(">4f", data, offset + 20))
        translation = list(struct.unpack_from(">3f", data, offset + 36))
        bones.append({"name": name, "parent": parent, "depth": depth,
                      "scale": scale, "rotation": rotation,
                      "translation": translation})
    return bones


def read_pas1(path: Path) -> tuple[int, dict]:
    data = path.read_bytes()
    if len(data) < 16:
        raise AssertionError("truncated PAS1 curves")
    magic, version, clip_count, joint_count, track_count, base_scale = struct.unpack_from(
        ">4sHHHHf", data, 0)
    if magic != b"PAS1" or version != 1 or clip_count != len(CLIPS) or base_scale != 64.0:
        raise AssertionError("unsupported PAS1 header")
    cursor = 16
    tracks = {}
    for _ in range(track_count):
        clip, bone, path_code, interpolation, count, reserved = struct.unpack_from(
            ">BBBBHH", data, cursor)
        cursor += 8
        if clip >= clip_count or bone >= joint_count or path_code > 2 or interpolation > 1 or reserved:
            raise AssertionError("invalid PAS1 track header")
        width = 4 if path_code == 1 else 3
        times, values = [], []
        for _ in range(count):
            times.append(struct.unpack_from(">f", data, cursor)[0])
            cursor += 4
            values.append(list(struct.unpack_from(">" + "f" * width, data, cursor)))
            cursor += width * 4
        identity = (clip, bone, path_code)
        if identity in tracks or not times or any(a >= b for a, b in zip(times, times[1:])):
            raise AssertionError("invalid/duplicate PAS1 curve")
        tracks[identity] = (interpolation, times, values)
    if cursor != len(data):
        raise AssertionError("PAS1 trailing bytes")
    return joint_count, tracks


def read_wsk1(path: Path) -> tuple[list[tuple[float, ...]], list[tuple]]:
    data = path.read_bytes()
    if len(data) < 16:
        raise AssertionError("truncated WSK1 skin")
    magic, version, joints, vertices, record_bytes, base_scale = struct.unpack_from(
        ">4sHHHHf", data, 0)
    if magic != b"WSK1" or version != 1 or record_bytes != 44 or base_scale != 64.0:
        raise AssertionError("unsupported WSK1 header")
    expected = 16 + joints * 64 + vertices * record_bytes
    if expected != len(data):
        raise AssertionError("malformed WSK1 size")
    inverse_binds = [struct.unpack_from(">16f", data, 16 + index * 64)
                     for index in range(joints)]
    records = [struct.unpack_from(">3f3f4B4f", data,
                                  16 + joints * 64 + index * record_bytes)
               for index in range(vertices)]
    return inverse_binds, records


def slerp(a: list[float], b: list[float], amount: float) -> list[float]:
    dot = sum(x * y for x, y in zip(a, b))
    sign = -1.0 if dot < 0.0 else 1.0
    dot = min(abs(dot), 1.0)
    left, right = 1.0 - amount, amount * sign
    if dot < 1.0 - 1.0e-6:
        angle = math.acos(dot)
        inverse_sine = 1.0 / math.sin(angle)
        left = math.sin((1.0 - amount) * angle) * inverse_sine
        right = math.sin(amount * angle) * inverse_sine * sign
    result = [x * left + y * right for x, y in zip(a, b)]
    norm = math.sqrt(sum(value * value for value in result))
    return [value / norm for value in result] if norm > 0 else result


def sample(interpolation: int, times: list[float], values: list[list[float]],
           time: float, rotation: bool) -> list[float]:
    if time <= times[0]:
        return values[0]
    if time >= times[-1]:
        return values[-1]
    right = next(index for index, key_time in enumerate(times) if key_time > time)
    left = right - 1
    if interpolation == 1:
        return values[left]
    amount = (time - times[left]) / (times[right] - times[left])
    if rotation:
        return slerp(values[left], values[right], amount)
    return [a + (b - a) * amount for a, b in zip(values[left], values[right])]


def matrix_srt(scale: list[float], rotation: list[float], translation: list[float]) -> list[float]:
    x, y, z, w = rotation
    xx, yy, zz = x * x, y * y, z * z
    xz, xy, yz = x * z, x * y, y * z
    wx, wy, wz = w * x, w * y, w * z
    return [
        (1 - 2 * (yy + zz)) * scale[0], 2 * (xy + wz) * scale[0],
        2 * (xz - wy) * scale[0], 0,
        2 * (xy - wz) * scale[1], (1 - 2 * (xx + zz)) * scale[1],
        2 * (yz + wx) * scale[1], 0,
        2 * (xz + wy) * scale[2], 2 * (yz - wx) * scale[2],
        (1 - 2 * (xx + yy)) * scale[2], 0,
        translation[0], translation[1], translation[2], 1,
    ]


def multiply(left: list[float], right: list[float]) -> list[float]:
    return [sum(left[k * 4 + row] * right[column * 4 + k] for k in range(4))
            for column in range(4) for row in range(4)]


def global_matrices(locals_: list[list[float]], bones: list[dict]) -> list[list[float]]:
    result = []
    for index, local in enumerate(locals_):
        parent = bones[index]["parent"]
        result.append(local if parent == 0xFFFF else multiply(result[parent], local))
    return result


def source_curves(document: dict, binary: bytes, animation: dict,
                  node_to_bone: dict[str, int]) -> tuple[dict, set[float]]:
    result = {}
    key_times: set[float] = {0.0}
    for channel in animation["channels"]:
        target = channel["target"]
        path = PATHS[target["path"]]
        node = document["nodes"][target["node"]]
        bone = node_to_bone[node["name"]]
        sampler_def = animation["samplers"][channel["sampler"]]
        times = [row[0] for row in _read_accessor(document, binary, sampler_def["input"])]
        values = _read_accessor(document, binary, sampler_def["output"])
        interpolation = 1 if sampler_def.get("interpolation", "LINEAR") == "STEP" else 0
        result[(bone, path)] = (interpolation, times, values)
        key_times.update(times)
    return result, key_times


def weighted_positions(globals_: list[list[float]], inverse_binds: list[tuple[float, ...]],
                       vertices: list[tuple]) -> list[tuple[float, float, float]]:
    skin_matrices = [multiply(global_, list(inverse_bind))
                     for global_, inverse_bind in zip(globals_, inverse_binds)]
    result = []
    for vertex in vertices:
        position, joints, weights = vertex
        output = [0.0, 0.0, 0.0]
        for joint, weight in zip(joints, weights):
            if weight == 0.0:
                break
            matrix = skin_matrices[joint]
            for row in range(3):
                output[row] += weight * (matrix[row] * position[0] +
                    matrix[4 + row] * position[1] + matrix[8 + row] * position[2] +
                    matrix[12 + row])
        result.append(tuple(output))
    return result


def blend_quaternion_source(a: list[float], b: list[float], amount: float) -> list[float]:
    """System.Numerics.Quaternion.Slerp from the source's pinned .NET 8 runtime."""
    cosine = sum(x * y for x, y in zip(a, b))
    flip = cosine < 0.0
    if flip:
        cosine = -cosine
    if cosine > 1.0 - 1.0e-6:
        left, right = 1.0 - amount, (-amount if flip else amount)
    else:
        angle = math.acos(cosine)
        inverse_sine = 1.0 / math.sin(angle)
        left = math.sin((1.0 - amount) * angle) * inverse_sine
        right = math.sin(amount * angle) * inverse_sine * (-1.0 if flip else 1.0)
    return [x * left + y * right for x, y in zip(a, b)]


def blend_pose_source(poses: list[tuple], weights: list[float]) -> tuple:
    """SharpGLTF AffineTransform.Blend, including S/T weights and quaternion fold order."""
    if len(poses) == 1:
        return poses[0]
    total = sum(weights)
    normalized = [weight / total for weight in weights] if total else weights[:]
    scale = [sum(pose[0][axis] * weight for pose, weight in zip(poses, normalized))
             for axis in range(3)]
    position = [sum(pose[1][axis] * weight for pose, weight in zip(poses, normalized))
                for axis in range(3)]
    rotation = poses[0][2][:]
    cumulative = normalized[0]
    for index in range(1, len(poses)):
        weight = normalized[index]
        cumulative += weight
        rotation = blend_quaternion_source(
            poses[index][2], rotation, weight / cumulative if cumulative else 0.0)
    norm = math.sqrt(sum(component * component for component in rotation))
    rotation = [component / norm for component in rotation]
    return scale, position, rotation


def f32(value: float) -> float:
    return struct.unpack("=f", struct.pack("=f", value))[0]


class SourceAnimationOracle:
    """Small float32 transcription of OG retained-track clock and weights."""

    def __init__(self, durations: list[float]):
        self.durations = [f32(duration) for duration in durations]
        self.tracks: list[dict] = []

    @staticmethod
    def blend_duration(a: int, b: int) -> float:
        pair = {CLIPS[a], CLIPS[b]}
        if pair == {"Idle", "Dash"}:
            return f32(0.05)
        if pair == {"Idle", "Run"}:
            return f32(0.2)
        if pair == {"Run", "Skid"}:
            return f32(0.125)
        return f32(0.01)

    def play(self, state: int) -> None:
        selected = {"state": state, "time": f32(0.0), "blend": f32(0.0)}
        for index, track in enumerate(self.tracks):
            if track["state"] == state:
                selected = self.tracks.pop(index)
                break
        selected["duration"] = self.durations[state]
        selected["loops"] = CLIPS[state] != "Dash"
        if not self.tracks:
            selected["blend"] = f32(1.0)
        selected["blend_duration"] = (
            self.blend_duration(state, self.tracks[-1]["state"])
            if self.tracks else f32(0.01)
        )
        self.tracks.append(selected)

    def advance(self, dt: float) -> None:
        if not self.tracks:
            return
        dt = f32(dt)
        blend_duration = self.tracks[-1]["blend_duration"]
        step = f32(dt / blend_duration)
        for index, track in enumerate(self.tracks):
            track["time"] = f32(track["time"] + dt)
            if track["time"] >= track["duration"]:
                track["time"] = (f32(track["time"] - track["duration"])
                                  if track["loops"] else track["duration"])
            target = f32(1.0 if index == len(self.tracks) - 1 else 0.0)
            if target > track["blend"]:
                track["blend"] = f32(min(target, f32(track["blend"] + step)))
            else:
                track["blend"] = f32(max(target, f32(track["blend"] - step)))

    def samples(self) -> list[tuple[int, float, float]]:
        return [
            (track["state"], f32(math.fmod(track["time"], track["duration"])),
             f32(1.0 - track["blend"]))
            for track in reversed(self.tracks) if track["blend"] > 0.0
        ]


def run_runtime_blend_fixture(runtime_poses_by_case: list[list[list[tuple]]],
                              case_weights: list[list[float]], bone_count: int) -> list[list[tuple]]:
    """Run production C++ blend helper against real curve samples from the shipped rig."""
    input_data = bytearray(struct.pack("=I", len(runtime_poses_by_case) * bone_count))
    for case_index, poses_by_clip in enumerate(runtime_poses_by_case):
        weights = case_weights[case_index]
        assert len(poses_by_clip) == len(weights)
        for bone_index in range(bone_count):
            input_data.extend(struct.pack("=I", len(weights)))
            for pose, weight in zip((poses[bone_index] for poses in poses_by_clip), weights):
                flat = (*pose[0], *pose[1], *pose[2])
                input_data.extend(struct.pack("=f10f", f32(weight), *(f32(v) for v in flat)))

    with tempfile.TemporaryDirectory(prefix="animated-blend-parity-") as temporary:
        binary = Path(temporary) / "animation_blend_batch"
        compile_result = subprocess.run(
            ["g++", "-std=c++17", f"-I{ROOT / 'src/user'}",
             str(ROOT / "tests/animation_blend_batch.cpp"), "-o", str(binary)],
            capture_output=True, text=True, check=False)
        if compile_result.returncode:
            raise AssertionError(f"blend helper compile failed:\n{compile_result.stderr}")
        result = subprocess.run([str(binary)], input=input_data, capture_output=True, check=False)
        if result.returncode:
            raise AssertionError(f"blend helper failed ({result.returncode}): {result.stderr!r}")

    record_count = len(runtime_poses_by_case) * bone_count
    if len(result.stdout) != record_count * 10 * 4:
        raise AssertionError(f"blend helper output size {len(result.stdout)} != {record_count * 40}")
    flat_results = [tuple(row) for row in struct.iter_unpack("=10f", result.stdout)]
    cases = []
    cursor = 0
    for _ in runtime_poses_by_case:
        bones = []
        for _ in range(bone_count):
            row = flat_results[cursor]
            cursor += 1
            bones.append((list(row[:3]), list(row[3:6]), list(row[6:10])))
        cases.append(bones)
    return cases


def clip_local_poses(clip_index: int, time: float, source_curves_by_clip: list[dict],
                     runtime_tracks: dict, nodes: list[dict], joint_nodes: list[int],
                     bones: list[dict], source_scale_tracks: list[dict],
                     source: bool) -> list[tuple]:
    curves = source_curves_by_clip[clip_index]
    output = []
    for bone_index, (node_index, bone) in enumerate(zip(joint_nodes, bones)):
        node = nodes[node_index]
        if source:
            position = [value * 64.0 for value in node.get("translation", [0.0] * 3)]
            rotation = node.get("rotation", [0.0, 0.0, 0.0, 1.0])[:]
            scale = node.get("scale", [1.0] * 3)[:]
        else:
            position, rotation, scale = (bone["translation"][:], bone["rotation"][:],
                                         bone["scale"][:])
        for path, target in ((0, position), (1, rotation), (2, scale)):
            source_curve = curves.get((bone_index, path))
            runtime_curve = runtime_tracks.get((clip_index, bone_index, path))
            if source_curve is None:
                assert runtime_curve is None
                continue
            assert runtime_curve is not None
            interpolation, times, values = source_curve if source else runtime_curve
            sampled = sample(interpolation, times, values, time, path == 1)
            factor = 64.0 if path == 0 else 1.0
            for axis in range(len(target)):
                target[axis] = sampled[axis] * factor
        bone_name = bones[bone_index]["name"]
        for track in source_scale_tracks:
            if track["clip"] != CLIPS[clip_index] or track["bone"] != bone_name:
                continue
            key = 0
            while key + 1 < len(track["times"]) and track["times"][key + 1] <= time:
                key += 1
            scale = list(track["values"][key])
        output.append((scale, position, rotation))
    return output


def main() -> int:
    glb = ROOT / "build/n64-prepared/player.glb"
    t3dm = ROOT / "assets/og_converted/models/player.t3dm"
    pas1 = ROOT / "assets/og_converted/models/player.anim"
    document, binary = read_glb(glb)
    nodes = document["nodes"]
    joint_nodes = document["skins"][0]["joints"]
    bones = read_t3dm_skeleton(t3dm)
    joint_count, runtime_tracks = read_pas1(pas1)
    runtime_inverse_binds, runtime_records = read_wsk1(
        ROOT / "assets/og_converted/models/player.wsk")
    assert joint_count == len(joint_nodes) == len(bones)
    assert len(runtime_inverse_binds) == joint_count
    assert len(runtime_records) > 0
    assert len(runtime_tracks) == 357
    node_to_bone = {nodes[node]["name"]: index for index, node in enumerate(joint_nodes)}
    assert [nodes[node]["name"] for node in joint_nodes] == [bone["name"] for bone in bones]
    parents = {child: parent for parent, node in enumerate(nodes)
               for child in node.get("children", [])}

    # Tiny3D consumes T3DM local SRTs with translations multiplied by baseScale.
    # Compare the shipped rig directly with prepared GLB rest transforms and
    # ancestry before validating animated samples.
    for index, (node_index, bone) in enumerate(zip(joint_nodes, bones)):
        node = nodes[node_index]
        parent_node = parents.get(node_index)
        expected_parent = node_to_bone[nodes[parent_node]["name"]] if parent_node in joint_nodes else 0xFFFF
        assert bone["parent"] == expected_parent, (bone["name"], bone["parent"], expected_parent)
        for actual, expected in zip(bone["scale"], node.get("scale", [1.0] * 3)):
            assert abs(actual - expected) < 2e-5, (bone["name"], "scale", actual, expected)
        for actual, expected in zip(bone["rotation"], node.get("rotation", [0, 0, 0, 1])):
            assert abs(actual - expected) < 2e-5, (bone["name"], "rotation", actual, expected)
        for actual, expected in zip(bone["translation"], node.get("translation", [0.0] * 3)):
            assert abs(actual - expected * 64.0) < 2e-4, (bone["name"], "translation", actual, expected)

    source_inverse_binds = _read_accessor(
        document, binary, document["skins"][0]["inverseBindMatrices"])
    expected_inverse_binds = [
        tuple(value * (64.0 if index in (12, 13, 14) else 1.0)
              for index, value in enumerate(matrix))
        for matrix in source_inverse_binds]
    for joint, (expected_inverse, runtime_inverse) in enumerate(
            zip(expected_inverse_binds, runtime_inverse_binds)):
        assert max(abs(a - b) for a, b in zip(expected_inverse, runtime_inverse)) < 2e-5, joint

    source_vertices = []
    for mesh in document["meshes"]:
        for primitive in mesh["primitives"]:
            attributes = primitive["attributes"]
            positions = _read_accessor(document, binary, attributes["POSITION"])
            joints = _read_accessor(document, binary, attributes["JOINTS_0"])
            weights = _read_accessor(document, binary, attributes["WEIGHTS_0"])
            for position, joint_row, weight_row in zip(positions, joints, weights):
                total = sum(weight_row)
                assert total > 0.0
                normalized = [weight / total for weight in weight_row]
                source_vertices.append((tuple(value * 64.0 for value in position),
                                        tuple(int(value) for value in joint_row),
                                        tuple(normalized)))
    assert len(source_vertices) == len(runtime_records)
    for index, (source_vertex, runtime_vertex) in enumerate(zip(source_vertices, runtime_records)):
        runtime_position = runtime_vertex[0:3]
        runtime_joints = runtime_vertex[6:10]
        runtime_weights = runtime_vertex[10:14]
        assert tuple(source_vertex[1]) == tuple(runtime_joints), ("joint lanes", index)
        assert max(abs(a - b) for a, b in zip(source_vertex[0], runtime_position)) < 2e-5, index
        assert max(abs(a - b) for a, b in zip(source_vertex[2], runtime_weights)) < 2e-5, index

    animations = {animation["name"]: animation for animation in document["animations"]}
    source_durations, source_scale_tracks = source_metadata(
        ROOT / "Celeste64-og/Content/Models/player.glb")
    source_curves_by_clip = []
    source_keys_by_clip = []
    for clip in CLIPS:
        curves, keys = source_curves(document, binary, animations[clip], node_to_bone)
        source_curves_by_clip.append(curves)
        source_keys_by_clip.append(keys)
    max_error = 0.0
    max_vertex_error = 0.0
    compared = 0
    compared_vertices = 0
    for clip_index, clip in enumerate(CLIPS):
        source = source_curves_by_clip[clip_index]
        keys = source_keys_by_clip[clip_index]
        duration = max(max(times) for _, times, _ in source.values())
        assert abs(duration - source_durations[clip_index]) < 1e-5, (clip, duration, source_durations[clip_index])
        ordered = sorted(keys | {duration})
        sample_times = set(ordered)
        sample_times.update((a + b) * 0.5 for a, b in zip(ordered, ordered[1:]))
        for time in sorted(sample_times):
            source_locals, runtime_locals = [], []
            for bone_index, (node_index, bone) in enumerate(zip(joint_nodes, bones)):
                node = nodes[node_index]
                src_t = [value * 64.0 for value in node.get("translation", [0.0] * 3)]
                src_r = node.get("rotation", [0.0, 0.0, 0.0, 1.0])[:]
                src_s = node.get("scale", [1.0, 1.0, 1.0])[:]
                run_t, run_r, run_s = bone["translation"][:], bone["rotation"][:], bone["scale"][:]
                for path, source_value, runtime_value in (
                    (0, src_t, run_t), (1, src_r, run_r), (2, src_s, run_s)
                ):
                    source_curve = source.get((bone_index, path))
                    runtime_curve = runtime_tracks.get((clip_index, bone_index, path))
                    assert (source_curve is None) == (runtime_curve is None), (clip, bone["name"], path)
                    if source_curve is None:
                        continue
                    src_interp, src_times, src_values = source_curve
                    run_interp, run_times, run_values = runtime_curve
                    source_sample = sample(src_interp, src_times, src_values, time, path == 1)
                    runtime_sample = sample(run_interp, run_times, run_values, time, path == 1)
                    factor = 64.0 if path == 0 else 1.0
                    for axis in range(len(source_value)):
                        source_value[axis] = source_sample[axis] * factor
                        runtime_value[axis] = runtime_sample[axis] * factor
                source_locals.append(matrix_srt(src_s, src_r, src_t))
                runtime_locals.append(matrix_srt(run_s, run_r, run_t))

            source_globals = global_matrices(source_locals, bones)
            runtime_globals = global_matrices(runtime_locals, bones)
            for bone_index, (expected, actual) in enumerate(zip(source_globals, runtime_globals)):
                error = max(abs(a - b) for a, b in zip(expected, actual))
                max_error = max(max_error, error)
                compared += 1
                assert error <= 5e-4, (clip, time, bones[bone_index]["name"], error)

            # Full weighted mesh at seven representative timeline positions;
            # global matrices above still cover every key and midpoint.
            if any(abs(time - duration * fraction) < 1e-6
                   for fraction in (0.0, 0.125, 0.25, 0.5, 0.75, 0.875, 1.0)):
                source_positions = weighted_positions(
                    source_globals, expected_inverse_binds, source_vertices)
                runtime_positions = weighted_positions(
                    runtime_globals, runtime_inverse_binds,
                    [(record[0:3], record[6:10], record[10:14]) for record in runtime_records])
                for vertex_index, (expected, actual) in enumerate(zip(source_positions, runtime_positions)):
                    error = max(abs(a - b) for a, b in zip(expected, actual))
                    max_vertex_error = max(max_vertex_error, error)
                    compared_vertices += 1
                    assert error <= 0.02, (clip, time, vertex_index, error)

    # Exercise the production C++ blend helper with actual source/runtime poses.
    # The source weights follow SkinnedModel.GetBlendedWeights' reverse track
    # order and inverse Blend values, then SharpGLTF normalizes and blends SRT.
    transition_cases = []
    runtime_inputs = []
    source_blended_cases = []
    blend_fractions = (0.125, 0.5, 0.875)
    for old_index, old_clip in enumerate(CLIPS):
        for new_index, new_clip in enumerate(CLIPS):
            if old_index == new_index:
                continue
            blend_duration = 0.01
            if {old_clip, new_clip} == {"Idle", "Dash"}:
                blend_duration = 0.05
            elif {old_clip, new_clip} == {"Idle", "Run"}:
                blend_duration = 0.2
            elif {old_clip, new_clip} == {"Run", "Skid"}:
                blend_duration = 0.125
            for fraction in blend_fractions:
                dt = f32(f32(blend_duration) * fraction)
                blend_step = f32(dt / f32(blend_duration))
                old_blend = f32(max(0.0, 1.0 - blend_step))
                new_blend = f32(min(1.0, blend_step))
                weights = [f32(1.0 - new_blend), f32(1.0 - old_blend)]
                old_duration = source_durations[old_index]
                new_duration = source_durations[new_index]
                old_time = f32(math.fmod(f32(old_duration * 0.37 + dt), old_duration))
                new_time = f32(math.fmod(dt, new_duration))
                source_new = clip_local_poses(
                    new_index, new_time, source_curves_by_clip, runtime_tracks,
                    nodes, joint_nodes, bones, source_scale_tracks, True)
                source_old = clip_local_poses(
                    old_index, old_time, source_curves_by_clip, runtime_tracks,
                    nodes, joint_nodes, bones, source_scale_tracks, True)
                runtime_new = clip_local_poses(
                    new_index, new_time, source_curves_by_clip, runtime_tracks,
                    nodes, joint_nodes, bones, source_scale_tracks, False)
                runtime_old = clip_local_poses(
                    old_index, old_time, source_curves_by_clip, runtime_tracks,
                    nodes, joint_nodes, bones, source_scale_tracks, False)
                transition_cases.append((old_clip, new_clip, fraction, weights))
                runtime_inputs.append([runtime_new, runtime_old])
                source_blended_cases.append([
                    blend_pose_source([new_pose, old_pose], weights)
                    for new_pose, old_pose in zip(source_new, source_old)
                ])

    runtime_blended_cases = run_runtime_blend_fixture(
        runtime_inputs, [case[3] for case in transition_cases], joint_count)
    blend_max_local_error = 0.0
    blend_max_matrix_error = 0.0
    blend_max_vertex_error = 0.0
    blend_joint_poses = 0
    blend_vertex_poses = 0
    runtime_skin_vertices = [
        (record[0:3], record[6:10], record[10:14]) for record in runtime_records
    ]
    for case_index, ((old_clip, new_clip, fraction, _), source_poses, runtime_poses) in enumerate(
            zip(transition_cases, source_blended_cases, runtime_blended_cases)):
        source_locals = [matrix_srt(scale, rotation, position)
                         for scale, position, rotation in source_poses]
        runtime_locals = [matrix_srt(scale, rotation, position)
                          for scale, position, rotation in runtime_poses]
        for bone_index, (expected_pose, actual_pose) in enumerate(zip(source_poses, runtime_poses)):
            expected_flat = (*expected_pose[0], *expected_pose[1], *expected_pose[2])
            actual_flat = (*actual_pose[0], *actual_pose[1], *actual_pose[2])
            error = max(abs(a - b) for a, b in zip(expected_flat, actual_flat))
            blend_max_local_error = max(blend_max_local_error, error)
            assert error <= 2e-4, (old_clip, new_clip, fraction, bones[bone_index]["name"], error)
        source_globals = global_matrices(source_locals, bones)
        runtime_globals = global_matrices(runtime_locals, bones)
        for bone_index, (expected, actual) in enumerate(zip(source_globals, runtime_globals)):
            error = max(abs(a - b) for a, b in zip(expected, actual))
            blend_max_matrix_error = max(blend_max_matrix_error, error)
            blend_joint_poses += 1
            assert error <= 0.003, (old_clip, new_clip, fraction, bones[bone_index]["name"], error)
        source_positions = weighted_positions(source_globals, expected_inverse_binds, source_vertices)
        runtime_positions = weighted_positions(runtime_globals, runtime_inverse_binds, runtime_skin_vertices)
        for vertex_index, (expected, actual) in enumerate(zip(source_positions, runtime_positions)):
            error = max(abs(a - b) for a, b in zip(expected, actual))
            blend_max_vertex_error = max(blend_max_vertex_error, error)
            blend_vertex_poses += 1
            assert error <= 0.03, (old_clip, new_clip, fraction, vertex_index, error)

    # Stress source-valid interrupted tracks with three, five, and all seven
    # retained clips active. Pairwise transitions above cannot catch an
    # order-dependent quaternion fold or normalization bug in larger blends.
    multi_track_oracle = SourceAnimationOracle(source_durations)
    multi_track_runtime_inputs = []
    multi_track_source_cases = []
    multi_track_labels = []
    multi_track_weights = []

    def capture_multitrack(expected_count: int) -> None:
        samples = multi_track_oracle.samples()
        if len(samples) != expected_count:
            raise AssertionError(
                f"source track oracle expected {expected_count} active tracks, got {len(samples)}")
        source_poses_by_track, runtime_poses_by_track = [], []
        for clip_index, time, _ in samples:
            source_poses_by_track.append(clip_local_poses(
                clip_index, time, source_curves_by_clip, runtime_tracks,
                nodes, joint_nodes, bones, source_scale_tracks, True))
            runtime_poses_by_track.append(clip_local_poses(
                clip_index, time, source_curves_by_clip, runtime_tracks,
                nodes, joint_nodes, bones, source_scale_tracks, False))
        weights = [weight for _, _, weight in samples]
        multi_track_labels.append(tuple(CLIPS[index] for index, _, _ in samples))
        multi_track_weights.append(weights)
        multi_track_runtime_inputs.append(runtime_poses_by_track)
        multi_track_source_cases.append([
            blend_pose_source([pose[bone] for pose in source_poses_by_track], weights)
            for bone in range(joint_count)
        ])

    multi_track_oracle.play(CLIPS.index("Idle"))
    multi_track_oracle.advance(0.1)
    multi_track_oracle.play(CLIPS.index("Dash"))
    multi_track_oracle.advance(0.005)
    multi_track_oracle.play(CLIPS.index("Run"))
    multi_track_oracle.advance(0.0005)
    capture_multitrack(3)
    multi_track_oracle.play(CLIPS.index("Skid"))
    multi_track_oracle.advance(0.0005)
    multi_track_oracle.play(CLIPS.index("Jump"))
    multi_track_oracle.advance(0.00002)
    capture_multitrack(5)
    multi_track_oracle.play(CLIPS.index("Climb.Idle"))
    multi_track_oracle.advance(0.000005)
    multi_track_oracle.play(CLIPS.index("Climb.Up"))
    multi_track_oracle.advance(0.0000005)
    capture_multitrack(7)

    multi_track_runtime_cases = run_runtime_blend_fixture(
        multi_track_runtime_inputs, multi_track_weights, joint_count)
    multi_track_max_local_error = 0.0
    multi_track_max_matrix_error = 0.0
    multi_track_max_vertex_error = 0.0
    multi_track_joint_poses = 0
    multi_track_vertex_poses = 0
    for labels, source_poses, runtime_poses in zip(
            multi_track_labels, multi_track_source_cases, multi_track_runtime_cases):
        source_locals = [matrix_srt(scale, rotation, position)
                         for scale, position, rotation in source_poses]
        runtime_locals = [matrix_srt(scale, rotation, position)
                          for scale, position, rotation in runtime_poses]
        for bone_index, (expected_pose, actual_pose) in enumerate(zip(source_poses, runtime_poses)):
            expected_flat = (*expected_pose[0], *expected_pose[1], *expected_pose[2])
            actual_flat = (*actual_pose[0], *actual_pose[1], *actual_pose[2])
            error = max(abs(a - b) for a, b in zip(expected_flat, actual_flat))
            multi_track_max_local_error = max(multi_track_max_local_error, error)
            assert error <= 2e-4, (labels, bones[bone_index]["name"], error)
        source_globals = global_matrices(source_locals, bones)
        runtime_globals = global_matrices(runtime_locals, bones)
        for bone_index, (expected, actual) in enumerate(zip(source_globals, runtime_globals)):
            error = max(abs(a - b) for a, b in zip(expected, actual))
            multi_track_max_matrix_error = max(multi_track_max_matrix_error, error)
            multi_track_joint_poses += 1
            assert error <= 0.003, (labels, bones[bone_index]["name"], error)
        source_positions = weighted_positions(source_globals, expected_inverse_binds, source_vertices)
        runtime_positions = weighted_positions(runtime_globals, runtime_inverse_binds, runtime_skin_vertices)
        for vertex_index, (expected, actual) in enumerate(zip(source_positions, runtime_positions)):
            error = max(abs(a - b) for a, b in zip(expected, actual))
            multi_track_max_vertex_error = max(multi_track_max_vertex_error, error)
            multi_track_vertex_poses += 1
            assert error <= 0.03, (labels, vertex_index, error)

    print(f"animated_joint_pose_parity_smoke: PASS ({compared} joint poses, max matrix error {max_error:.3g}; "
          f"{compared_vertices} weighted vertex poses, max position error {max_vertex_error:.3g}; "
          f"{blend_joint_poses} blended joint poses, max local/matrix error "
          f"{blend_max_local_error:.3g}/{blend_max_matrix_error:.3g}; "
          f"{blend_vertex_poses} blended weighted vertices, max position error {blend_max_vertex_error:.3g})")
    print(f"  interrupted {len(multi_track_labels)} multi-track poses "
          f"({', '.join(str(len(labels)) for labels in multi_track_labels)} tracks), "
          f"{multi_track_joint_poses} joints, max local/matrix error "
          f"{multi_track_max_local_error:.3g}/{multi_track_max_matrix_error:.3g}; "
          f"{multi_track_vertex_poses} weighted vertices, max position error "
          f"{multi_track_max_vertex_error:.3g}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
