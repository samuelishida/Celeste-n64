#!/usr/bin/env python3
"""Validate weighted-skin sidecar mapping against prepared GLB and T3DM."""

from __future__ import annotations

import math
from pathlib import Path
import struct
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

from build_player_skin_sidecar import HEADER, RECORD, build_sidecar  # noqa: E402
from glb_assets import read_glb  # noqa: E402


def main() -> int:
    glb = ROOT / "build/n64-prepared/player.glb"
    t3dm = ROOT / "assets/og_converted/models/player.t3dm"
    with tempfile.TemporaryDirectory(prefix="weighted-skin-smoke-") as temp_dir:
        sidecar = Path(temp_dir) / "player.wsk"
        report = build_sidecar(glb, t3dm, sidecar)
        data = sidecar.read_bytes()

    assert report == {
        "joint_count": 17,
        "source_vertex_count": 2492,
        "packed_vertex_count": 2688,
        "multi_influence_vertex_count": 1481,
        "first_joint_is_max_count": 2492,
        "max_dominant_position_error": 0.0,
        "dominant_normal_mismatch_count": 15,
        "max_dominant_normal_component_error": 1,
        "sidecar_bytes": HEADER.size + 17 * 64 + 2492 * RECORD.size,
    }, report

    magic, version, joints, vertices, record_size, base_scale = HEADER.unpack_from(data)
    assert (magic, version, joints, vertices, record_size) == (b"WSK1", 1, 17, 2492, 44)
    assert math.isclose(base_scale, 64.0)
    matrix_bytes = joints * 16 * 4
    records_offset = HEADER.size + matrix_bytes
    assert len(data) == HEADER.size + matrix_bytes + vertices * record_size
    inverse_binds = struct.unpack_from(f">{joints * 16}f", data, HEADER.size)
    assert all(math.isfinite(value) for value in inverse_binds)

    def multiply(left: tuple[float, ...], right: tuple[float, ...]) -> tuple[float, ...]:
        return tuple(
            sum(left[inner * 4 + row] * right[column * 4 + inner] for inner in range(4))
            for column in range(4)
            for row in range(4)
        )

    globals_: list[tuple[float, ...]] = []
    for joint in range(joints):
        angle = (joint + 1) * 0.013
        cosine, sine = math.cos(angle), math.sin(angle)
        globals_.append((
            cosine, sine, 0.0, 0.0,
            -sine, cosine, 0.0, 0.0,
            0.0, 0.0, 1.0 + joint * 0.0001, 0.0,
            joint * 0.07, -joint * 0.03, joint * 0.02, 1.0,
        ))
    skin_matrices = [
        multiply(globals_[joint], inverse_binds[joint * 16:(joint + 1) * 16])
        for joint in range(joints)
    ]
    max_shader_sum_error = 0.0
    for index in range(vertices):
        record = RECORD.unpack_from(data, records_offset + index * record_size)
        position = record[:3]
        influence_joints, weights = record[6:10], record[10:14]
        blended = [0.0] * 16
        for joint, weight in zip(influence_joints, weights):
            for component in range(16):
                blended[component] += skin_matrices[joint][component] * weight
        for row in range(3):
            shader_sum = (blended[row] * position[0] + blended[4 + row] * position[1] +
                          blended[8 + row] * position[2] + blended[12 + row])
            distributed_sum = sum(
                weights[lane] * (
                    skin_matrices[joint][row] * position[0] +
                    skin_matrices[joint][4 + row] * position[1] +
                    skin_matrices[joint][8 + row] * position[2] +
                    skin_matrices[joint][12 + row]
                )
                for lane, joint in enumerate(influence_joints)
            )
            error = abs(shader_sum - distributed_sum)
            max_shader_sum_error = max(max_shader_sum_error, error)
            assert error <= 2e-5, (index, row, shader_sum, distributed_sum, error)

    document, binary = read_glb(glb)
    from build_player_skin_sidecar import _read_accessor  # noqa: E402
    source_ibms = _read_accessor(document, binary, document["skins"][0]["inverseBindMatrices"])
    for joint in range(joints):
        for component, index in enumerate((12, 13, 14)):
            assert math.isclose(inverse_binds[joint * 16 + index], source_ibms[joint][index] * base_scale,
                                rel_tol=1e-6, abs_tol=1e-5)
        for index in range(16):
            if index not in (12, 13, 14):
                assert math.isclose(inverse_binds[joint * 16 + index], source_ibms[joint][index],
                                    rel_tol=1e-6, abs_tol=1e-6)

    multiple = 0
    for index in range(vertices):
        record = RECORD.unpack_from(data, records_offset + index * record_size)
        position, normal = record[:3], record[3:6]
        influence_joints, weights = record[6:10], record[10:14]
        assert all(math.isfinite(value) for value in (*position, *normal, *weights))
        assert all(joint < joints for joint in influence_joints)
        assert all(weight >= 0.0 for weight in weights)
        zero_seen = False
        for weight in weights:
            if weight == 0.0:
                zero_seen = True
            elif zero_seen:
                raise AssertionError(f"source vertex {index}: active skin lane follows zero-weight lane")
        assert math.isclose(sum(weights), 1.0, abs_tol=2e-6)
        multiple += sum(weight > 1e-6 for weight in weights) > 1
    assert multiple == report["multi_influence_vertex_count"]
    print("weighted_skin_asset_smoke: PASS (2492 source vertices, 2688 packed vertices, "
          f"1481 weighted vertices, max shader-sum error {max_shader_sum_error:.2g})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
