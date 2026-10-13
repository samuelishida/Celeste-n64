#!/usr/bin/env python3
"""Prepare OG GLB models for tiny3d.

Static-player mode performs a bind-pose bake in pinned Blender/Fast64. An
already-unskinned GLB is accepted through deterministic host path so CI can
qualify generated fixtures without Blender; skinned sources fail closed when
locked Blender executable unavailable.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any

from glb_assets import GlbError, embedded_image_info, extract_embedded_image, read_glb, write_glb


ROOT = Path(__file__).resolve().parents[1]
LOCK_PATH = ROOT / "tools" / "asset_toolchain.lock.json"


def _finite(value: Any) -> bool:
    return isinstance(value, (int, float)) and math.isfinite(float(value))


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _json_extra(material: dict[str, Any]) -> None:
    """Write explicit texture-free Fast64 shade metadata into glTF extras."""
    shade_color = {"name": "", "A": 0, "B": 0, "C": 0, "D": 4}
    record = {
        "presetName": "Custom",
        "combiner1": {**shade_color, "A_alpha": 0, "B_alpha": 0, "C_alpha": 0, "D_alpha": 4},
        "combiner2": {"name": "", "A": 0, "B": 0, "C": 0, "D": 0, "A_alpha": 0, "B_alpha": 0, "C_alpha": 0, "D_alpha": 0},
        "set_prim": False,
        "set_env": False,
        "set_blend": False,
        "prim_color": [1.0, 1.0, 1.0, 1.0],
        "env_color": [0.0, 0.0, 0.0, 1.0],
        "blend_color": [0.0, 0.0, 0.0, 1.0],
        "draw_layer": {"name": "", "sm64": 0, "oot": 0},
        "rdp_settings": {
            "name": "", "g_zbuffer": 1, "g_shade": 1, "g_cull_front": 0, "g_cull_back": 0,
            "g_fog": 0, "g_tex_gen": 0, "g_mdsft_text_filt": 0, "g_mdsft_cycletype": 0,
            "set_rendermode": 1, "rendermode_preset_cycle_1": 1, "rendermode_preset_cycle_2": 1,
        },
        "f3d_draw_flags": ["SHADED", "DEPTH"],
        "f3d_combiner": "RDPQ_COMBINER_SHADE",
        "f3d_blend": "OPAQUE",
    }
    material["extras"] = {
        "f3d_mat": record,
        "data": {"f3d_mat": {"combiner": "RDPQ_COMBINER_SHADE"}},
        "f3d_combiner": "RDPQ_COMBINER_SHADE",
        "f3d_draw_flags": ["SHADED", "DEPTH"],
        "f3d_blend": "OPAQUE",
    }


def _accessor_bounds(document: dict[str, Any], binary: bytes, accessor_index: Any) -> tuple[list[float], list[float], int]:
    accessors = document.get("accessors", [])
    if not isinstance(accessor_index, int) or not 0 <= accessor_index < len(accessors):
        raise ValueError(f"accessor {accessor_index}: index out of range")
    accessor = accessors[accessor_index]
    minimum, maximum = accessor.get("min"), accessor.get("max")
    if isinstance(minimum, list) and isinstance(maximum, list) and len(minimum) == len(maximum):
        if not all(_finite(value) for value in minimum + maximum):
            raise ValueError(f"accessor {accessor_index}: non-finite bounds")
        return [float(v) for v in minimum], [float(v) for v in maximum], int(accessor.get("count", 0))
    # Blender omits min/max for normals and UVs. Decode float vectors to keep
    # finite-value validation strict without requiring pygltflib.
    if accessor.get("componentType") != 5126:
        raise ValueError(f"accessor {accessor_index}: finite min/max required")
    dimensions = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}.get(accessor.get("type"))
    if dimensions is None:
        raise ValueError(f"accessor {accessor_index}: unsupported type")
    views = document.get("bufferViews", [])
    view_index = accessor.get("bufferView")
    if not isinstance(view_index, int) or not 0 <= view_index < len(views):
        raise ValueError(f"accessor {accessor_index}: bufferView missing")
    view = views[view_index]
    count = int(accessor.get("count", 0))
    stride = int(view.get("byteStride", dimensions * 4))
    start = int(view.get("byteOffset", 0)) + int(accessor.get("byteOffset", 0))
    end = start + max(0, count - 1) * stride + dimensions * 4
    if count <= 0 or start < 0 or end > len(binary):
        raise ValueError(f"accessor {accessor_index}: data out of BIN range")
    low = [float("inf")] * dimensions
    high = [float("-inf")] * dimensions
    for index in range(count):
        values = struct.unpack_from(f"<{dimensions}f", binary, start + index * stride)
        if not all(math.isfinite(value) for value in values):
            raise ValueError(f"accessor {accessor_index}: non-finite value")
        for axis, value in enumerate(values):
            low[axis] = min(low[axis], value)
            high[axis] = max(high[axis], value)
    return low, high, count


def _accessor_count(document: dict[str, Any], accessor_index: Any) -> int:
    accessors = document.get("accessors", [])
    if not isinstance(accessor_index, int) or not 0 <= accessor_index < len(accessors):
        raise ValueError(f"accessor {accessor_index}: index out of range")
    count = int(accessors[accessor_index].get("count", 0))
    if count <= 0:
        raise ValueError(f"accessor {accessor_index}: empty index data")
    return count


def _validate_color_accessor(document: dict[str, Any], binary: bytes, accessor_index: Any, expected_count: int, source: Path) -> None:
    accessors = document.get("accessors", [])
    if not isinstance(accessor_index, int) or not 0 <= accessor_index < len(accessors):
        raise ValueError(f"{source}: COLOR_0 accessor index out of range")
    accessor = accessors[accessor_index]
    if accessor.get("type") != "VEC4" or int(accessor.get("count", 0)) != expected_count:
        raise ValueError(f"{source}: COLOR_0 must be VEC4 with POSITION count")
    component_type = accessor.get("componentType")
    if component_type not in (5121, 5123, 5126):
        raise ValueError(f"{source}: COLOR_0 uses unsupported componentType={component_type}")
    if component_type in (5121, 5123) and not accessor.get("normalized"):
        raise ValueError(f"{source}: integer COLOR_0 accessor must be normalized")
    views = document.get("bufferViews", [])
    view_index = accessor.get("bufferView")
    if not isinstance(view_index, int) or not 0 <= view_index < len(views):
        raise ValueError(f"{source}: COLOR_0 bufferView missing")
    view = views[view_index]
    component_bytes = {5121: 1, 5123: 2, 5126: 4}[component_type]
    stride = int(view.get("byteStride", 4 * component_bytes))
    start = int(view.get("byteOffset", 0)) + int(accessor.get("byteOffset", 0))
    end = start + max(0, expected_count - 1) * stride + 4 * component_bytes
    if expected_count <= 0 or start < 0 or end > len(binary):
        raise ValueError(f"{source}: COLOR_0 data out of BIN range")
    for index in range(expected_count):
        if component_type == 5121:
            values = tuple(binary[start + index * stride + channel] / 255.0 for channel in range(4))
        elif component_type == 5123:
            values = struct.unpack_from("<4H", binary, start + index * stride)
            values = tuple(value / 65535.0 for value in values)
        else:
            values = struct.unpack_from("<4f", binary, start + index * stride)
        if not all(math.isfinite(value) and 0.0 <= value <= 1.0 for value in values):
            raise ValueError(f"{source}: COLOR_0 has non-finite or out-of-range channel")
        if abs(values[3] - 1.0) > 1e-4:
            raise ValueError(f"{source}: COLOR_0 alpha must be opaque")


def _validate_static_glb(path: Path, recipe: dict[str, Any]) -> dict[str, Any]:
    document, binary = read_glb(path)
    meshes = document.get("meshes") or []
    nodes = document.get("nodes") or []
    materials = document.get("materials") or []
    if not meshes or not materials:
        raise ValueError(f"{path}: static GLB requires non-empty meshes and materials")
    if document.get("skins"):
        raise ValueError(f"{path}: skins remain after static bake")
    if document.get("animations"):
        raise ValueError(f"{path}: animations remain after static bake")
    if document.get("images") or document.get("textures"):
        raise ValueError(f"{path}: texture/image dependencies remain after flat-color bake")
    vertex_count = 0
    triangle_count = 0
    bounds_min = [float("inf")] * 3
    bounds_max = [float("-inf")] * 3
    for mesh in meshes:
        for primitive in mesh.get("primitives", []):
            attributes = primitive.get("attributes", {})
            if any(name in attributes for name in ("JOINTS_0", "JOINTS_1", "WEIGHTS_0", "WEIGHTS_1")):
                raise ValueError(f"{path}: skin attributes remain in primitive")
            position_index = attributes.get("POSITION")
            if position_index is None:
                raise ValueError(f"{path}: primitive has no POSITION accessor")
            minimum, maximum, count = _accessor_bounds(document, binary, position_index)
            if len(minimum) != 3 or count <= 0:
                raise ValueError(f"{path}: invalid POSITION accessor")
            vertex_count += count
            color_index = attributes.get("COLOR_0")
            if color_index is None or _accessor_count(document, color_index) != count:
                raise ValueError(f"{path}: COLOR_0 missing or does not match POSITION count")
            _validate_color_accessor(document, binary, color_index, count, path)
            if "TEXCOORD_0" in attributes:
                raise ValueError(f"{path}: TEXCOORD_0 remains after flat-color bake")
            for axis in range(3):
                bounds_min[axis] = min(bounds_min[axis], minimum[axis])
                bounds_max[axis] = max(bounds_max[axis], maximum[axis])
            index_index = primitive.get("indices")
            if index_index is not None:
                triangle_count += _accessor_count(document, index_index) // 3
            else:
                triangle_count += count // 3
    if vertex_count <= 0 or triangle_count <= 0 or any(not math.isfinite(v) for v in bounds_min + bounds_max):
        raise ValueError(f"{path}: empty or non-finite geometry")
    if triangle_count > int(recipe.get("max_triangles", 4096)):
        raise ValueError(f"{path}: triangle count {triangle_count} exceeds static-player budget")
    for material_index, material in enumerate(materials):
        if not isinstance(material, dict) or not material.get("name"):
            raise ValueError(f"{path}: material {material_index} missing stable name")
        extras = material.get("extras", {})
        try:
            f3d_mat = extras["f3d_mat"]
            if "tex0" in f3d_mat or "tex_reference" in extras or "tex0" in extras.get("data", {}).get("f3d_mat", {}):
                raise KeyError("texture reference remains")
            if f3d_mat.get("f3d_combiner") != "RDPQ_COMBINER_SHADE" or "TEXTURED" in f3d_mat.get("f3d_draw_flags", []):
                raise KeyError("shade combiner/draw flags")
        except (KeyError, TypeError) as exc:
            raise ValueError(f"{path}: material {material_index} missing Fast64 flat-color metadata: {exc}") from exc
    prepared_bytes = path.stat().st_size
    max_bytes = int(recipe.get("max_prepared_bytes", 256 * 1024))
    if prepared_bytes > max_bytes:
        raise ValueError(f"{path}: prepared GLB {prepared_bytes} bytes exceeds {max_bytes}")
    return {
        "asset_id": recipe["asset_id"],
        "source": recipe.get("source_id", Path(recipe["source"]).name),
        "prepared": path.name,
        "preparation": recipe.get("preparation", "blender-rest-bind-pose-static-bake"),
        "mesh_count": len(meshes),
        "node_count": len(nodes),
        "material_names": [str(item["name"]) for item in materials],
        "image_dependencies": [],
        "skin_present": False,
        "animation_count": 0,
        "vertex_count": vertex_count,
        "triangle_count": triangle_count,
        "prepared_glb_bytes": prepared_bytes,
        "bounds": {"min": bounds_min, "max": bounds_max},
        "pivot": recipe.get("pivot", "origin"),
        "base_scale": float(recipe.get("base_scale", 5.0)),
        "axis_transform": recipe.get("axis_transform", "game-y-up-xz-travel"),
        "flat_shading": True,
        "color_source": f"{Path(recipe['source']).name}#image[{int(recipe.get('texture_image_index', 0))}]",
        "color_source_sha256": recipe["color_source_sha256"],
        "vertex_color_attribute": "COLOR_0",
        "alpha_policy": "opaque",
        "source_sha256": _sha256(Path(recipe["source"])),
        "prepared_sha256": _sha256(path),
    }


def _host_static_fixture(source: Path, recipe: dict[str, Any], output: Path) -> None:
    document, binary = read_glb(source)
    if document.get("skins") or document.get("animations"):
        raise RuntimeError("static-player source is skinned; install locked Blender via ./setup.sh")
    for material in document.get("materials", []):
        _json_extra(material)
        pbr = material.get("pbrMetallicRoughness", {})
        pbr.pop("baseColorTexture", None)
        pbr.setdefault("baseColorFactor", [1.0, 1.0, 1.0, 1.0])
    for mesh in document.get("meshes", []):
        for primitive in mesh.get("primitives", []):
            primitive.get("attributes", {}).pop("TEXCOORD_0", None)
    document.pop("images", None)
    document.pop("textures", None)
    document.pop("skins", None)
    document.pop("animations", None)
    for node in document.get("nodes", []):
        node.pop("skin", None)
    output.parent.mkdir(parents=True, exist_ok=True)
    write_glb(output, document, binary)


def _blender_binary() -> Path | None:
    explicit = os.environ.get("BLENDER_BIN")
    if explicit:
        return Path(explicit)
    lock = json.loads(LOCK_PATH.read_text(encoding="utf-8"))
    configured = lock.get("tools", {}).get("blender", {}).get("binary")
    candidates = []
    if configured:
        candidates.append(Path(configured).expanduser())
    version = lock.get("tools", {}).get("blender", {}).get("version", "4.0.2")
    bootstrap = Path(lock.get("n64_inst", "~/.n64-bootstrap/opt/libdragon")).expanduser().parents[1]
    candidates.extend([
        bootstrap / f"blender-{version}-linux-x64" / "blender",
        bootstrap / "blender" / "blender",
        Path("/usr/bin/blender"),
    ])
    for candidate in candidates:
        if candidate.is_file() and os.access(candidate, os.X_OK):
            return candidate
    return None


def _run_blender(source: Path, recipe: dict[str, Any], output: Path, report_path: Path) -> None:
    blender = _blender_binary()
    if blender is None:
        raise RuntimeError("locked Blender 4.0.2 not found; run ./setup.sh")
    with tempfile.TemporaryDirectory(prefix="static-player-") as temp_dir:
        recipe_path = Path(temp_dir) / "recipe.json"
        recipe_path.write_text(json.dumps(recipe, sort_keys=True), encoding="utf-8")
        command = [
            str(blender), "--background", "--factory-startup", "--python",
            str(ROOT / "tools" / "blender" / "prepare_static_player.py"), "--",
            "--source", str(source), "--output", str(output), "--report", str(report_path),
            "--recipe", str(recipe_path),
        ]
        result = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        tail = "\n".join(result.stdout.splitlines()[-40:])
        if result.returncode != 0:
            raise RuntimeError(f"Blender static bake failed ({result.returncode}):\n{tail}")
    if not output.is_file() or not report_path.is_file():
        raise RuntimeError(f"Blender static bake returned without output/report:\n{tail}")


def _normalize_fast64_metadata(path: Path) -> None:
    """Normalize exporter custom properties into locked Fast64 extras shape."""
    document, binary = read_glb(path)
    for material in document.get("materials", []):
        _json_extra(material)
    write_glb(path, document, binary)


def _append_skin_source_vertex_ids(document: dict[str, Any], binary: bytes, source: Path) -> tuple[bytes, int]:
    """Encode stable source vertex IDs in UVs unused by the shade-only player.

    Tiny3D reorders and duplicates packed vertices while grouping by bone.
    TEXCOORD_0.x carries each prepared GLB vertex's identity through that
    conversion. Keep the coordinate integer-exact after Tiny3D's 32*32 scale.
    """
    buffers = document.get("buffers", [])
    if len(buffers) != 1 or buffers[0].get("uri") is not None:
        raise ValueError(f"{source}: animated player requires one embedded GLB buffer")
    meshes = document.get("meshes", [])
    if not meshes:
        raise ValueError(f"{source}: animated player has no meshes for skin IDs")
    mesh_users: dict[int, int] = {}
    for node in document.get("nodes", []):
        mesh_index = node.get("mesh")
        if mesh_index is not None:
            mesh_users[mesh_index] = mesh_users.get(mesh_index, 0) + 1
    if any(mesh_users.get(index) != 1 for index in range(len(meshes))):
        raise ValueError(f"{source}: skin source IDs require each mesh to have exactly one scene instance")

    output = bytearray(binary)
    source_id = 0
    for mesh_index, mesh in enumerate(meshes):
        for primitive_index, primitive in enumerate(mesh.get("primitives", [])):
            attributes = primitive.get("attributes", {})
            if "TEXCOORD_0" in attributes:
                raise ValueError(f"{source}: primitive already has TEXCOORD_0; refusing to overwrite source data")
            position_accessor = attributes.get("POSITION")
            if position_accessor is None:
                raise ValueError(f"{source}: mesh {mesh_index} primitive {primitive_index} has no POSITION")
            count = _accessor_count(document, position_accessor)
            if source_id + count > 0x7FFF:
                raise ValueError(f"{source}: {source_id + count} skin IDs exceed signed T3D UV range")
            byte_offset = len(output)
            for vertex in range(count):
                # The importer packs s = uv.x * 32 texels * 32 fixed-point units.
                output.extend(struct.pack("<ff", (source_id + vertex) / 1024.0, 0.0))
            buffer_view_index = len(document.setdefault("bufferViews", []))
            document["bufferViews"].append({
                "buffer": 0,
                "byteOffset": byte_offset,
                "byteLength": count * 8,
                "target": 34962,
            })
            accessor_index = len(document.setdefault("accessors", []))
            document["accessors"].append({
                "bufferView": buffer_view_index,
                "componentType": 5126,
                "count": count,
                "type": "VEC2",
                "min": [source_id / 1024.0, 0.0],
                "max": [(source_id + count - 1) / 1024.0, 0.0],
                "extras": {
                    "n64SkinSourceVertexBase": source_id,
                    "n64SkinSourceVertexCount": count,
                },
            })
            attributes["TEXCOORD_0"] = accessor_index
            source_id += count

    document["buffers"][0]["byteLength"] = len(output)
    return bytes(output), source_id


def prepare_static_player(source: Path, recipe: dict[str, Any], out_dir: Path) -> dict[str, Any]:
    source = source.resolve()
    if not source.is_file():
        raise FileNotFoundError(source)
    out_dir.mkdir(parents=True, exist_ok=True)
    embedded_image_info(source, int(recipe.get("texture_image_index", 0)))
    output = out_dir / f"{recipe['asset_id']}.glb"
    report_path = out_dir / f"{recipe['asset_id']}.report.json"
    # Blender chooses export format from filename suffix; keep temporary GLB
    # suffix so it does not append a second extension.
    temp_output = out_dir / f".{recipe['asset_id']}.tmp.glb"
    temp_report = out_dir / f".{recipe['asset_id']}.tmp.report.json"
    for stale in (temp_output, temp_report):
        stale.unlink(missing_ok=True)
    document, _ = read_glb(source)
    recipe = dict(recipe)
    recipe["source"] = str(source)
    try:
        recipe["source_id"] = source.relative_to(ROOT).as_posix()
    except ValueError:
        recipe["source_id"] = source.name
    recipe.setdefault("asset_id", source.stem)
    image_index = int(recipe.get("texture_image_index", 0))
    with tempfile.TemporaryDirectory(prefix="static-player-image-") as temp_dir:
        source_image_path = extract_embedded_image(source, image_index, Path(temp_dir) / "source-image")
        recipe["color_source_sha256"] = _sha256(source_image_path)
    worker_report: dict[str, Any] = {}
    try:
        if document.get("skins") or document.get("animations"):
            _run_blender(source, recipe, temp_output, temp_report)
            worker_report = json.loads(temp_report.read_text(encoding="utf-8"))
            if int(worker_report.get("color_samples", 0)) <= 0:
                raise ValueError("Blender static bake did not report sampled face colors")
            if worker_report.get("color_source_sha256") != recipe["color_source_sha256"]:
                raise ValueError("Blender static bake source image hash mismatch")
            _normalize_fast64_metadata(temp_output)
        else:
            _host_static_fixture(source, recipe, temp_output)
            temp_report.write_text("{}\n", encoding="utf-8")
        prepared = _validate_static_glb(temp_output, recipe)
        if worker_report:
            prepared["color_samples"] = int(worker_report["color_samples"])
        prepared["source_sha256"] = _sha256(source)
        prepared["prepared_sha256"] = _sha256(temp_output)
        prepared["prepared"] = output.name
        prepared["preparation"] = "blender-rest-bind-pose-static-bake" if document.get("skins") or document.get("animations") else "host-unskinned-static-fixture"
        output.unlink(missing_ok=True)
        report_path.unlink(missing_ok=True)
        temp_output.replace(output)
        report_path.write_text(json.dumps(prepared, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        image_path = out_dir / f"{recipe['asset_id']}.images" / "image_0.png"
        extract_embedded_image(source, image_index, image_path)
        return prepared
    except Exception:
        temp_output.unlink(missing_ok=True)
        temp_report.unlink(missing_ok=True)
        raise


def _normalize_skin_root(path: Path) -> dict[str, Any]:
    """Move a translation-only skin ancestor into joint roots and their clips.

    Tiny3D's animated skinning consumes joint model matrices. Removing a
    transformed ancestor without migrating it changes every skin matrix; root
    translation channels also replace, rather than add to, the bind transform.
    """
    document, binary = read_glb(path)
    skins = document.get("skins", [])
    if not skins or not skins[0].get("joints"):
        return {"changed": False}
    joints = set(skins[0]["joints"])
    parents: dict[int, int] = {}
    for parent, node in enumerate(document.get("nodes", [])):
        for child in node.get("children", []):
            parents[int(child)] = parent
    joint_roots = [joint for joint in joints if parents.get(joint) not in joints]
    migrated_ancestors: dict[int, dict[str, Any]] = {}
    root_offsets: dict[int, tuple[float, float, float]] = {}
    for root in joint_roots:
        ancestor = parents.get(root)
        if ancestor is None:
            continue
        node = document["nodes"][ancestor]
        transform = {key: node[key] for key in ("translation", "rotation", "scale") if key in node}
        if not transform:
            continue
        if "matrix" in node:
            raise ValueError(f"skin root ancestor {ancestor} uses unsupported matrix transform")
        rotation = node.get("rotation", [0.0, 0.0, 0.0, 1.0])
        scale = node.get("scale", [1.0, 1.0, 1.0])
        if len(rotation) != 4 or any(abs(float(a) - b) > 1e-7 for a, b in zip(rotation, [0.0, 0.0, 0.0, 1.0])):
            raise ValueError(f"skin root ancestor {ancestor} rotation cannot be normalized safely")
        if len(scale) != 3 or any(abs(float(a) - 1.0) > 1e-7 for a in scale):
            raise ValueError(f"skin root ancestor {ancestor} scale cannot be normalized safely")
        translation = node.get("translation", [0.0, 0.0, 0.0])
        if len(translation) != 3 or not all(_finite(value) for value in translation):
            raise ValueError(f"skin root ancestor {ancestor} has invalid translation")
        offset = tuple(float(value) for value in translation)
        if ancestor in migrated_ancestors and migrated_ancestors[ancestor]["offset"] != offset:
            raise ValueError(f"skin root ancestor {ancestor} changed during normalization")
        migrated_ancestors[ancestor] = {
            "node": ancestor,
            "name": node.get("name", ""),
            "transform": transform,
            "offset": offset,
        }
        root_offsets[root] = offset

    if not migrated_ancestors:
        return {"changed": False}

    # Translate bind-pose joint roots. Matrix-form joint roots would require
    # decomposition and are rejected instead of silently losing the offset.
    for root, offset in root_offsets.items():
        root_node = document["nodes"][root]
        if "matrix" in root_node:
            raise ValueError(f"skin joint root {root} uses unsupported matrix transform")
        translation = root_node.get("translation", [0.0, 0.0, 0.0])
        if len(translation) != 3 or not all(_finite(value) for value in translation):
            raise ValueError(f"skin joint root {root} has invalid translation")
        root_node["translation"] = [float(value) + delta for value, delta in zip(translation, offset)]

    # Animation channels override a node's bind translation. Carry the same
    # offset into every root-joint translation key, once per shared accessor.
    animation_offsets: dict[int, tuple[float, float, float]] = {}
    animation_targets: dict[int, set[tuple[int, str]]] = {}
    for animation in document.get("animations", []):
        for channel in animation.get("channels", []):
            target = channel.get("target", {})
            node_index = target.get("node")
            if node_index not in root_offsets or target.get("path") != "translation":
                continue
            samplers = animation.get("samplers", [])
            sampler_index = channel.get("sampler")
            if not isinstance(sampler_index, int) or not 0 <= sampler_index < len(samplers):
                raise ValueError("skin-root translation channel has invalid sampler")
            output = samplers[sampler_index].get("output")
            if not isinstance(output, int) or not 0 <= output < len(document.get("accessors", [])):
                raise ValueError("skin-root translation channel has invalid output accessor")
            offset = root_offsets[node_index]
            if output in animation_offsets and animation_offsets[output] != offset:
                raise ValueError(f"shared animation accessor {output} has conflicting root offsets")
            animation_offsets[output] = offset
            animation_targets.setdefault(output, set()).add((node_index, "translation"))

    for animation in document.get("animations", []):
        for channel in animation.get("channels", []):
            sampler_index = channel.get("sampler")
            samplers = animation.get("samplers", [])
            if not isinstance(sampler_index, int) or not 0 <= sampler_index < len(samplers):
                continue
            output = samplers[sampler_index].get("output")
            if output in animation_offsets:
                target = channel.get("target", {})
                if (target.get("node"), target.get("path")) not in animation_targets[output]:
                    raise ValueError(f"animation accessor {output} is shared with another target")

    binary = bytearray(binary)
    for accessor_index, offset in animation_offsets.items():
        accessor = document["accessors"][accessor_index]
        if accessor.get("componentType") != 5126 or accessor.get("type") != "VEC3" or "sparse" in accessor:
            raise ValueError(f"skin-root translation accessor {accessor_index} must be FLOAT VEC3")
        view_index = accessor.get("bufferView")
        if not isinstance(view_index, int) or not 0 <= view_index < len(document.get("bufferViews", [])):
            raise ValueError(f"skin-root translation accessor {accessor_index} has invalid bufferView")
        view = document["bufferViews"][view_index]
        stride = int(view.get("byteStride", 12))
        start = int(view.get("byteOffset", 0)) + int(accessor.get("byteOffset", 0))
        count = int(accessor.get("count", 0))
        if count <= 0 or stride < 12 or start < 0 or start + (count - 1) * stride + 12 > len(binary):
            raise ValueError(f"skin-root translation accessor {accessor_index} exceeds binary bounds")
        for index in range(count):
            values = struct.unpack_from("<3f", binary, start + index * stride)
            if not all(math.isfinite(value) for value in values):
                raise ValueError(f"skin-root translation accessor {accessor_index} has non-finite key")
            struct.pack_into("<3f", binary, start + index * stride,
                             *(value + delta for value, delta in zip(values, offset)))
        for bound in ("min", "max"):
            if bound in accessor:
                accessor[bound] = [float(value) + delta for value, delta in zip(accessor[bound], offset)]

    for ancestor in migrated_ancestors:
        node = document["nodes"][ancestor]
        for key in ("translation", "rotation", "scale"):
            node.pop(key, None)
    if migrated_ancestors:
        document_path = path.with_suffix(".normalized.glb")
        write_glb(document_path, document, binary)
        document_path.replace(path)
    return {
        "changed": bool(migrated_ancestors),
        "roots": list(migrated_ancestors.values()),
        "compensated_joint_roots": sorted(root_offsets),
        "adjusted_translation_accessors": sorted(animation_offsets),
    }


def _pad_animation_starts(path: Path) -> list[str]:
    """Make glTF's pre-first-key hold explicit for Tiny3D's stream decoder.

    The importer assumes every channel starts at zero and computes duration as
    end-start. OG Skid starts at 1/24s; duplicate its first value at zero so the
    stream preserves the original absolute timeline and initial clamped pose.
    """
    document, original_binary = read_glb(path)
    binary = bytearray(original_binary)
    changed = []

    def prepend(accessor_index: int, is_time: bool) -> int:
        accessor = document["accessors"][accessor_index]
        if accessor["componentType"] != 5126 or "sparse" in accessor:
            raise ValueError("Animation padding requires nonsparse FLOAT accessors")
        width = {"SCALAR": 1, "VEC3": 3, "VEC4": 4}[accessor["type"]]
        view = document["bufferViews"][accessor["bufferView"]]
        stride = view.get("byteStride", width * 4)
        offset = view.get("byteOffset", 0) + accessor.get("byteOffset", 0)
        rows = [bytes(binary[offset + i * stride:offset + i * stride + width * 4])
                for i in range(accessor["count"])]
        if not rows or any(len(row) != width * 4 for row in rows):
            raise ValueError("Animation padding accessor exceeds binary bounds")
        first = struct.pack("<f", 0.0) if is_time else rows[0]
        while len(binary) % 4:
            binary.append(0)
        start = len(binary)
        binary.extend(first + b"".join(rows))
        document["bufferViews"].append({"buffer": 0, "byteOffset": start,
                                        "byteLength": len(first) + sum(map(len, rows))})
        replacement = {"bufferView": len(document["bufferViews"]) - 1,
                       "componentType": 5126, "count": len(rows) + 1,
                       "type": accessor["type"]}
        for key in ("min", "max"):
            if key in accessor:
                replacement[key] = accessor[key][:]
        if is_time:
            replacement["min"] = [0.0]
        document["accessors"].append(replacement)
        return len(document["accessors"]) - 1

    for animation in document.get("animations", []):
        modified = False
        for sampler in animation["samplers"]:
            accessor = document["accessors"][sampler["input"]]
            view = document["bufferViews"][accessor["bufferView"]]
            offset = view.get("byteOffset", 0) + accessor.get("byteOffset", 0)
            start = struct.unpack_from("<f", binary, offset)[0]
            if start <= 0:
                continue
            if sampler.get("interpolation", "LINEAR") not in ("LINEAR", "STEP"):
                raise ValueError("Cubic animation start padding requires tangent support")
            sampler["input"] = prepend(sampler["input"], True)
            sampler["output"] = prepend(sampler["output"], False)
            modified = True
        if modified:
            changed.append(animation.get("name", ""))
    if changed:
        document["buffers"][0]["byteLength"] = len(binary)
        write_glb(path, document, bytes(binary))
    return changed


def _validate_step_scale_basis(source: Path, prepared: Path) -> float:
    """Fail if Blender changed the local scale basis used by source overrides."""
    from generate_player_animation_source import accessor_values
    source_doc, source_bin = read_glb(source)
    prepared_doc, prepared_bin = read_glb(prepared)
    prepared_animations = {item["name"]: item for item in prepared_doc["animations"]}
    maximum_error = 0.0
    for animation in source_doc["animations"]:
        if animation["name"] not in prepared_animations:
            continue
        converted = prepared_animations[animation["name"]]
        targets = {}
        for channel in converted["channels"]:
            if channel["target"]["path"] == "scale":
                name = prepared_doc["nodes"][channel["target"]["node"]]["name"]
                targets[name] = converted["samplers"][channel["sampler"]]
        for channel in animation["channels"]:
            sampler = animation["samplers"][channel["sampler"]]
            if channel["target"]["path"] != "scale" or sampler.get("interpolation") != "STEP":
                continue
            name = source_doc["nodes"][channel["target"]["node"]]["name"]
            if name not in targets:
                raise ValueError(f"Prepared STEP scale joint missing: {name}")
            expected = accessor_values(source_doc, source_bin, sampler["output"])
            actual = accessor_values(prepared_doc, prepared_bin, targets[name]["output"])
            if len(expected) != len(actual):
                raise ValueError(f"Prepared STEP scale keys changed: {name}")
            error = max(abs(a - b) for row_a, row_b in zip(expected, actual) for a, b in zip(row_a, row_b))
            maximum_error = max(maximum_error, error)
            if error > 0.00001:
                raise ValueError(f"Prepared STEP scale basis changed: {name}, error={error}")
    return maximum_error


def prepare_animated_model(source: Path, recipe: dict[str, Any], out_dir: Path) -> dict[str, Any]:
    """Bake flat vertex colors while retaining the player's rig and clips."""
    source = source.resolve()
    if not source.is_file():
        raise FileNotFoundError(source)
    out_dir.mkdir(parents=True, exist_ok=True)
    prepared = out_dir / f"{recipe.get('asset_id', source.stem)}.glb"
    report_path = out_dir / f"{recipe.get('asset_id', source.stem)}.report.json"
    temp_output = out_dir / f".{recipe.get('asset_id', source.stem)}.animated.tmp.glb"
    temp_report = out_dir / f".{recipe.get('asset_id', source.stem)}.animated.tmp.report.json"
    for stale in (temp_output, temp_report):
        stale.unlink(missing_ok=True)
    document, _ = read_glb(source)
    image_index = int(recipe.get("texture_image_index", 0))
    with tempfile.TemporaryDirectory(prefix="animated-player-image-") as temp_dir:
        source_image = extract_embedded_image(source, image_index, Path(temp_dir) / "source-image")
        color_source_sha256 = _sha256(source_image)
    recipe = dict(recipe)
    recipe["source"] = str(source)
    try:
        recipe["source_id"] = source.relative_to(ROOT).as_posix()
    except ValueError:
        recipe["source_id"] = source.name
    blender = _blender_binary()
    if blender is None:
        raise RuntimeError("locked Blender 4.0.2 not found; run ./setup.sh")
    with tempfile.TemporaryDirectory(prefix="animated-player-") as temp_dir:
        command = [
            str(blender), "--background", "--factory-startup", "--python",
            str(ROOT / "tools" / "blender" / "prepare_animated_player.py"), "--",
            "--source", str(source), "--output", str(temp_output), "--report", str(temp_report),
        ]
        result = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        tail = "\n".join(result.stdout.splitlines()[-50:])
        if result.returncode != 0:
            raise RuntimeError(f"Blender animated color bake failed ({result.returncode}):\n{tail}")
    worker_report = json.loads(temp_report.read_text(encoding="utf-8"))
    normalized = _normalize_skin_root(temp_output)
    step_scale_basis_error = _validate_step_scale_basis(source, temp_output)
    padded_animation_starts = _pad_animation_starts(temp_output)
    prepared_doc, binary = read_glb(temp_output)
    animations = prepared_doc.get("animations", [])
    clip_names = [item.get("name", f"clip_{index}") for index, item in enumerate(animations)]
    required = recipe.get("required_animations", [])
    missing = [item["source"] for item in required if isinstance(item, dict) and item.get("source") not in clip_names]
    if missing:
        raise ValueError(f"{source}: required animation clips missing: {', '.join(missing)}")
    skins = prepared_doc.get("skins", [])
    if not skins or not skins[0].get("joints"):
        raise ValueError(f"{source}: animated model has no skeleton joints")
    if not animations:
        raise ValueError(f"{source}: Blender export dropped all animation clips")
    if prepared_doc.get("images") or prepared_doc.get("textures"):
        raise ValueError(f"{source}: animated flat-color GLB retains image/texture dependencies")
    vertex_count = 0
    for mesh in prepared_doc.get("meshes", []):
        for primitive in mesh.get("primitives", []):
            attributes = primitive.get("attributes", {})
            position_index = attributes.get("POSITION")
            color_index = attributes.get("COLOR_0")
            if position_index is None or color_index is None:
                raise ValueError(f"{source}: animated primitive is missing POSITION or COLOR_0")
            count = _accessor_count(prepared_doc, position_index)
            if _accessor_count(prepared_doc, color_index) != count:
                raise ValueError(f"{source}: animated COLOR_0 does not match POSITION count")
            _validate_color_accessor(prepared_doc, binary, color_index, count, source)
            if "TEXCOORD_0" in attributes or "JOINTS_0" not in attributes or "WEIGHTS_0" not in attributes:
                raise ValueError(f"{source}: animated primitive lost its rig or retained UVs")
            vertex_count += count
    if vertex_count <= 0 or int(worker_report.get("color_samples", 0)) <= 0:
        raise ValueError(f"{source}: animated color bake produced no colored geometry")
    _normalize_fast64_metadata(temp_output)
    prepared_doc, binary = read_glb(temp_output)
    binary, skin_source_vertex_count = _append_skin_source_vertex_ids(prepared_doc, binary, source)
    write_glb(temp_output, prepared_doc, binary)
    prepared_doc, binary = read_glb(temp_output)
    report = {
        "asset_id": recipe.get("asset_id", source.stem), "source": recipe["source_id"], "prepared": prepared.name,
        "preparation": "blender-animated-flat-color-bake", "mesh_count": len(prepared_doc.get("meshes", [])),
        "node_count": len(prepared_doc.get("nodes", [])), "images": 0,
        "animation_count": len(animations), "clip_names": clip_names,
        "joint_count": len(skins[0]["joints"]), "vertex_count": vertex_count,
        "normalized_skin_root": normalized,
        "padded_animation_starts": padded_animation_starts,
        "step_scale_basis_max_error": step_scale_basis_error,
        "color_samples": int(worker_report["color_samples"]),
        "color_source_sha256": color_source_sha256,
        "prepared_sha256": _sha256(temp_output), "source_sha256": _sha256(source),
        "binary_bytes": len(binary), "image_dependencies": [],
        "vertex_color_attribute": "COLOR_0", "alpha_policy": "opaque", "animated": True,
        "skin_source_vertex_ids": {
            "attribute": "TEXCOORD_0.x",
            "packed_s_offset": 0,
            "packed_s_units_per_id": 1,
            "source_vertex_count": skin_source_vertex_count,
        },
    }
    temp_output.replace(prepared)
    report_path.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return report


def _recipe_from_args(args: argparse.Namespace) -> tuple[Path, dict[str, Any], Path]:
    if args.static_player:
        if not args.source or not args.recipe_json:
            raise ValueError("--static-player requires --source and --recipe-json")
        recipe = json.loads(Path(args.recipe_json).read_text(encoding="utf-8"))
        if not isinstance(recipe, dict):
            raise ValueError("recipe JSON root must be object")
        recipe["asset_id"] = args.asset_id
        return Path(args.source), recipe, Path(args.out_dir)
    manifest_path = Path(args.manifest)
    from asset_manifest import load_manifest, validate_manifest
    manifest = load_manifest(manifest_path)
    diagnostics = validate_manifest(manifest, check_outputs=False)
    if diagnostics:
        raise ValueError("manifest validation failed: " + "; ".join(diagnostics))
    asset = next((item for item in manifest["assets"] if item.get("id") == args.asset_id), None)
    if asset is None:
        raise ValueError(f"unknown asset id: {args.asset_id}")
    source = (manifest_path.parents[1] / ".." / asset["source"]).resolve()
    recipe = dict(asset.get("transform", {}))
    recipe.update({"asset_id": args.asset_id, "required_animations": asset.get("animations", []), "profile": asset.get("profile")})
    return source, recipe, Path(args.out_dir)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--static-player", action="store_true")
    parser.add_argument("--source")
    parser.add_argument("--recipe-json")
    parser.add_argument("--manifest", default="assets/og_converted/asset_manifest.json")
    parser.add_argument("--asset-id", required=True)
    parser.add_argument("--out-dir", default="build/n64-prepared")
    args = parser.parse_args(argv)
    try:
        source, recipe, out_dir = _recipe_from_args(args)
        report = prepare_static_player(source, recipe, out_dir) if args.static_player or recipe.get("profile") == "static_player" else prepare_animated_model(source, recipe, out_dir)
        print(json.dumps(report, sort_keys=True))
        return 0
    except (OSError, ValueError, RuntimeError, GlbError, json.JSONDecodeError) as exc:
        print(f"prepare_n64_model: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
