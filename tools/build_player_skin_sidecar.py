#!/usr/bin/env python3
"""Build validated CPU-skin input for the textureless animated player."""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
import struct
import sys
from typing import Any

from glb_assets import read_glb


HEADER = struct.Struct(">4sHHHHf")
RECORD = struct.Struct(">3f3f4B4f")
T3DM_VERTEX_BYTES = 32
MAX_SOURCE_ID = 0x7FFF


def _read_accessor(document: dict[str, Any], binary: bytes, index: int) -> list[tuple[float, ...]]:
    accessors = document.get("accessors", [])
    if not isinstance(index, int) or not 0 <= index < len(accessors):
        raise ValueError(f"accessor index {index!r} is invalid")
    accessor = accessors[index]
    if "sparse" in accessor:
        raise ValueError(f"accessor {index}: sparse data is unsupported")
    views = document.get("bufferViews", [])
    view_index = accessor.get("bufferView")
    if not isinstance(view_index, int) or not 0 <= view_index < len(views):
        raise ValueError(f"accessor {index}: bufferView is missing")
    view = views[view_index]
    width = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}.get(accessor.get("type"))
    component_type = accessor.get("componentType")
    component = {5121: (1, "B"), 5123: (2, "H"), 5126: (4, "f")}.get(component_type)
    if width is None or component is None:
        raise ValueError(f"accessor {index}: unsupported type/component {accessor.get('type')}/{component_type}")
    component_bytes, fmt = component
    component_count = width
    if accessor.get("type") == "MAT4":
        component_count = 16
    packed_size = component_count * component_bytes
    stride = int(view.get("byteStride", packed_size))
    start = int(view.get("byteOffset", 0)) + int(accessor.get("byteOffset", 0))
    count = int(accessor.get("count", 0))
    end = start + max(0, count - 1) * stride + packed_size
    if count <= 0 or stride < packed_size or start < 0 or end > len(binary):
        raise ValueError(f"accessor {index}: data is empty, strided incorrectly, or outside GLB BIN")

    rows: list[tuple[float, ...]] = []
    normalized = bool(accessor.get("normalized", False))
    for row_index in range(count):
        values = struct.unpack_from("<" + fmt * component_count, binary, start + row_index * stride)
        if normalized:
            if component_type == 5121:
                values = tuple(value / 255.0 for value in values)
            elif component_type == 5123:
                values = tuple(value / 65535.0 for value in values)
            else:
                raise ValueError(f"accessor {index}: FLOAT data cannot be normalized")
        row = tuple(float(value) for value in values)
        if not all(math.isfinite(value) for value in row):
            raise ValueError(f"accessor {index}: non-finite value")
        rows.append(row)
    return rows


def _matrix_vector(matrix: tuple[float, ...], vector: tuple[float, float, float], *, point: bool) -> tuple[float, float, float]:
    w = 1.0 if point else 0.0
    result = tuple(
        matrix[row] * vector[0] + matrix[4 + row] * vector[1] + matrix[8 + row] * vector[2] + matrix[12 + row] * w
        for row in range(3)
    )
    return result


def _round_away(value: float) -> int:
    return math.floor(value + 0.5) if value >= 0.0 else math.ceil(value - 0.5)


def _pack_normal(normal: tuple[float, float, float]) -> int:
    length = math.sqrt(sum(value * value for value in normal))
    if not math.isfinite(length) or length <= 1e-12:
        raise ValueError("transformed normal is degenerate")
    scales = (15.5, 31.5, 15.5)
    limits = ((-16, 15), (-32, 31), (-16, 15))
    packed = []
    for value, scale, (low, high) in zip(normal, scales, limits):
        component = max(low, min(high, _round_away(value / length * scale)))
        packed.append(component)
    return ((packed[0] & 31) << 11) | ((packed[1] & 63) << 5) | (packed[2] & 31)


def _unpack_normal(packed: int) -> tuple[int, int, int]:
    def signed(value: int, bits: int) -> int:
        return value - (1 << bits) if value & (1 << (bits - 1)) else value
    return (
        signed((packed >> 11) & 31, 5),
        signed((packed >> 5) & 63, 6),
        signed(packed & 31, 5),
    )


def _node_has_transform(node: dict[str, Any]) -> bool:
    if "matrix" in node:
        matrix = node["matrix"]
        identity = (1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1)
        if len(matrix) != 16 or any(abs(float(a) - b) > 1e-7 for a, b in zip(matrix, identity)):
            return True
    translation = node.get("translation", (0, 0, 0))
    rotation = node.get("rotation", (0, 0, 0, 1))
    scale = node.get("scale", (1, 1, 1))
    return (
        len(translation) != 3 or any(abs(float(v)) > 1e-7 for v in translation)
        or len(rotation) != 4 or any(abs(float(v) - e) > 1e-7 for v, e in zip(rotation, (0, 0, 0, 1)))
        or len(scale) != 3 or any(abs(float(v) - 1.0) > 1e-7 for v in scale)
    )


def _validate_mesh_nodes(document: dict[str, Any]) -> None:
    parents: dict[int, int] = {}
    for parent_index, node in enumerate(document.get("nodes", [])):
        for child in node.get("children", []):
            if child in parents:
                raise ValueError(f"node {child} has multiple parents")
            parents[child] = parent_index
    meshes = document.get("meshes", [])
    users: dict[int, int] = {}
    mesh_nodes = []
    for node_index, node in enumerate(document.get("nodes", [])):
        if "mesh" not in node:
            continue
        mesh_index = int(node["mesh"])
        users[mesh_index] = users.get(mesh_index, 0) + 1
        mesh_nodes.append(node_index)
    if any(users.get(index) != 1 for index in range(len(meshes))):
        raise ValueError("each player mesh must have exactly one scene instance")
    for mesh_node in mesh_nodes:
        cursor: int | None = mesh_node
        while cursor is not None:
            node = document["nodes"][cursor]
            if _node_has_transform(node):
                raise ValueError(f"mesh node path contains unsupported transform at {node.get('name', cursor)}")
            cursor = parents.get(cursor)


def _packed_t3dm_vertices(path: Path) -> list[tuple[tuple[int, int, int], int, int]]:
    data = path.read_bytes()
    if len(data) < 48 or data[:4] != b"T3M\x04":
        raise ValueError(f"{path}: invalid T3DM header")
    chunk_count = struct.unpack_from(">I", data, 4)[0]
    vertex_count = struct.unpack_from(">H", data, 8)[0]
    vertex_offsets = []
    for index in range(chunk_count):
        word = struct.unpack_from(">I", data, 44 + index * 4)[0]
        if word >> 24 == ord("V"):
            vertex_offsets.append(word & 0x00FFFFFF)
    if len(vertex_offsets) != 1:
        raise ValueError(f"{path}: expected one shared vertex chunk")
    offset = vertex_offsets[0]
    if vertex_count <= 0 or offset + ((vertex_count - 1) // 2) * T3DM_VERTEX_BYTES + T3DM_VERTEX_BYTES > len(data):
        raise ValueError(f"{path}: packed vertex data is truncated")
    result = []
    for index in range(vertex_count):
        pair_offset = offset + (index // 2) * T3DM_VERTEX_BYTES
        if index & 1:
            position = struct.unpack_from(">3h", data, pair_offset + 8)
            normal = struct.unpack_from(">H", data, pair_offset + 14)[0]
            source_id = struct.unpack_from(">h", data, pair_offset + 28)[0]
        else:
            position = struct.unpack_from(">3h", data, pair_offset)
            normal = struct.unpack_from(">H", data, pair_offset + 6)[0]
            source_id = struct.unpack_from(">h", data, pair_offset + 24)[0]
        result.append((position, normal, source_id))
    return result


def build_sidecar(glb_path: Path, t3dm_path: Path, output_path: Path, base_scale: float = 64.0) -> dict[str, int | float]:
    if not math.isfinite(base_scale) or base_scale <= 0.0:
        raise ValueError(f"base scale must be finite and positive, got {base_scale}")
    document, binary = read_glb(glb_path)
    if document.get("images") or document.get("textures"):
        raise ValueError("weighted player GLB must remain textureless")
    if len(document.get("skins", [])) != 1:
        raise ValueError("weighted player requires exactly one skin")
    _validate_mesh_nodes(document)
    skin = document["skins"][0]
    joint_nodes = skin.get("joints", [])
    joint_count = len(joint_nodes)
    if not 0 < joint_count <= 256:
        raise ValueError(f"unsupported joint count {joint_count}")

    joint_set = set(joint_nodes)
    tree_order: list[int] = []
    def visit(node_index: int) -> None:
        if node_index in joint_set:
            tree_order.append(node_index)
        for child in document["nodes"][node_index].get("children", []):
            visit(child)
    visit(joint_nodes[0])
    if tree_order != joint_nodes:
        raise ValueError("Tiny3D skeleton traversal order differs from glTF skin joint indices")

    ibm_accessor = skin.get("inverseBindMatrices")
    if ibm_accessor is None:
        raise ValueError("weighted player skin has no inverse-bind matrices")
    source_inverse_binds = _read_accessor(document, binary, ibm_accessor)
    if len(source_inverse_binds) != joint_count or any(len(matrix) != 16 for matrix in source_inverse_binds):
        raise ValueError("inverse-bind matrix count/shape does not match the skin")
    # Tiny3D scales skeleton translations and animation translation keys by
    # base_scale. Conjugate each GLB inverse bind into the same model-space
    # units while keeping its linear basis unchanged.
    inverse_binds = [
        tuple(value * base_scale if index in (12, 13, 14) else value for index, value in enumerate(matrix))
        for matrix in source_inverse_binds
    ]

    records: list[tuple[tuple[float, float, float], tuple[float, float, float], tuple[int, ...], tuple[float, ...]] | None] = []
    source_base = 0
    multi_influence_count = 0
    first_joint_is_max = 0
    for mesh_index, mesh in enumerate(document.get("meshes", [])):
        for primitive_index, primitive in enumerate(mesh.get("primitives", [])):
            attributes = primitive.get("attributes", {})
            required = ("POSITION", "NORMAL", "JOINTS_0", "WEIGHTS_0", "TEXCOORD_0")
            if any(name not in attributes for name in required):
                raise ValueError(f"mesh {mesh_index} primitive {primitive_index}: required skin attributes missing")
            positions = _read_accessor(document, binary, attributes["POSITION"])
            normals = _read_accessor(document, binary, attributes["NORMAL"])
            joints = _read_accessor(document, binary, attributes["JOINTS_0"])
            weights = _read_accessor(document, binary, attributes["WEIGHTS_0"])
            uv_accessor = document["accessors"][attributes["TEXCOORD_0"]]
            base = uv_accessor.get("extras", {}).get("n64SkinSourceVertexBase")
            count = uv_accessor.get("extras", {}).get("n64SkinSourceVertexCount")
            if base != source_base or count != len(positions) or len({len(values) for values in (normals, joints, weights)}) != 1:
                raise ValueError(f"mesh {mesh_index} primitive {primitive_index}: inconsistent skin source ID range")
            ids = _read_accessor(document, binary, attributes["TEXCOORD_0"])
            if any(abs(row[0] * 1024.0 - (source_base + index)) > 1e-4 or abs(row[1]) > 1e-7
                   for index, row in enumerate(ids)):
                raise ValueError(f"mesh {mesh_index} primitive {primitive_index}: UV skin IDs are not stable")
            if not (len(positions) == len(normals) == len(joints) == len(weights) == len(ids)):
                raise ValueError(f"mesh {mesh_index} primitive {primitive_index}: attribute counts disagree")
            for local_index, (position, normal, joint_row, weight_row) in enumerate(zip(positions, normals, joints, weights)):
                if len(position) != 3 or len(normal) != 3 or len(joint_row) != 4 or len(weight_row) != 4:
                    raise ValueError("weighted player requires POSITION/NORMAL VEC3 and four skin influences")
                joint_indices = tuple(int(index) for index in joint_row)
                if any(index < 0 or index >= joint_count for index in joint_indices):
                    raise ValueError(f"source vertex {source_base + local_index}: joint index out of range")
                weight_sum = sum(weight_row)
                if not math.isfinite(weight_sum) or weight_sum <= 1e-8 or any(value < 0.0 for value in weight_row):
                    raise ValueError(f"source vertex {source_base + local_index}: invalid skin weights")
                normalized_weights = tuple(value / weight_sum for value in weight_row)
                if sum(value > 1e-6 for value in normalized_weights) > 1:
                    multi_influence_count += 1
                if joint_indices[0] == joint_indices[max(range(4), key=lambda lane: weight_row[lane])]:
                    first_joint_is_max += 1
                source_id = source_base + local_index
                if source_id != len(records):
                    raise ValueError("skin source IDs are not contiguous")
                records.append((tuple(position), tuple(normal), joint_indices, normalized_weights))
            source_base += len(positions)

    if not records or len(records) - 1 > MAX_SOURCE_ID or len(records) > 0xFFFF:
        raise ValueError(f"source vertex count {len(records)} exceeds supported marker range")
    if first_joint_is_max != len(records):
        raise ValueError(f"dominant-joint importer assumption changed: {first_joint_is_max}/{len(records)}")

    packed_vertices = _packed_t3dm_vertices(t3dm_path)
    packed_by_id: dict[int, list[tuple[tuple[int, int, int], int]]] = {}
    for position, normal, source_id in packed_vertices:
        packed_by_id.setdefault(source_id, []).append((position, normal))
    represented_ids = set(packed_by_id)
    if represented_ids != set(range(len(records))):
        raise ValueError("T3DM source IDs do not cover every GLB vertex at least once")

    max_position_error = 0.0
    normal_mismatches = 0
    max_normal_component_error = 0
    for source_id, (position, normal, joints, _) in enumerate(records):
        matrix = inverse_binds[joints[0]]
        scaled_position = tuple(value * base_scale for value in position)
        transformed = _matrix_vector(matrix, scaled_position, point=True)
        expected_position = tuple(_round_away(value) for value in transformed)
        expected_normal = _unpack_normal(_pack_normal(_matrix_vector(matrix, normal, point=False)))
        for actual_position, actual_normal in packed_by_id[source_id]:
            error = max(abs(actual_position[axis] - expected_position[axis]) for axis in range(3))
            max_position_error = max(max_position_error, float(error))
            if error != 0:
                raise ValueError(f"source vertex {source_id}: dominant bind position differs from T3DM by {error}")
            actual_components = _unpack_normal(actual_normal)
            component_error = max(abs(actual_components[axis] - expected_normal[axis]) for axis in range(3))
            max_normal_component_error = max(max_normal_component_error, component_error)
            if component_error:
                normal_mismatches += 1
            # The converter computes and rounds in float32; this verifier uses
            # Python doubles. Permit one packed component LSB for tie-boundary
            # cases, but fail if transform basis or source mapping drifts.
            if component_error > 1:
                raise ValueError(
                    f"source vertex {source_id}: dominant bind normal differs from T3DM by {component_error} LSB"
                )

    output = bytearray(HEADER.pack(b"WSK1", 1, joint_count, len(records), RECORD.size, base_scale))
    for matrix in inverse_binds:
        output.extend(struct.pack(">16f", *matrix))
    for position, normal, joints, weights in records:
        output.extend(RECORD.pack(
            *(value * base_scale for value in position), *normal, *joints, *weights
        ))
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_bytes(output)
    return {
        "joint_count": joint_count,
        "source_vertex_count": len(records),
        "packed_vertex_count": len(packed_vertices),
        "multi_influence_vertex_count": multi_influence_count,
        "first_joint_is_max_count": first_joint_is_max,
        "max_dominant_position_error": max_position_error,
        "dominant_normal_mismatch_count": normal_mismatches,
        "max_dominant_normal_component_error": max_normal_component_error,
        "sidecar_bytes": len(output),
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--glb", type=Path, required=True)
    parser.add_argument("--t3dm", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--base-scale", type=float, default=64.0)
    args = parser.parse_args(argv)
    try:
        print(json.dumps(build_sidecar(args.glb, args.t3dm, args.out, args.base_scale), sort_keys=True))
        return 0
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        print(f"build-player-skin-sidecar: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
