#!/usr/bin/env python3
"""GLB baker — brush geometry → .glb with smooth normals.

Generates a glTF 2.0 binary file from OG map brush geometry.
Each triangle gets proper UVs, smooth normals (averaged per vertex),
and material grouping by texture name.

The output .glb is converted to .t3dm by gltf_to_t3d from the toolchain.
"""

import sys
import os
import struct
import json
import math
import argparse
from pathlib import Path
from collections import defaultdict

sys.path.insert(0, str(Path(__file__).parent))

from ogmap_lib import (
    parse_map, classify_entity,
    MaterialClass, RenderMode, FaceFilter,
    compute_face_polygon, sort_vertices_ccw, dedupe_polygon_vertices,
    compute_uv, transform_point, transform_normal,
    fan_triangulate, build_material_manifest,
    Vec3,
)

# ── Geometry collection ─────────────────────────────────────────────

def collect_faces(parsed_map, scale):
    """Collect all renderable faces from the map, grouped by texture.

    Returns: {texture_name: [(verts, normals, uvs, indices), ...]}
    where each tuple is one face's data.
    """
    groups: dict[str, list] = defaultdict(list)

    for ent in parsed_map.entities:
        cd = classify_entity(ent)
        if cd is None:
            continue
        if cd.render_mode != RenderMode.STATIC_MESH:
            continue
        if not ent.brushes:
            continue

        for brush in ent.brushes:
            faces = brush.faces
            for face_idx, face in enumerate(faces):
                if cd.face_filter == FaceFilter.UPWARD_ONLY and not transform_normal(face["normal"])[1] > 0.3:
                    continue

                polygon = compute_face_polygon(faces, face_idx)
                if len(polygon) < 3:
                    continue
                polygon = sort_vertices_ccw(polygon, face["normal"])
                polygon = dedupe_polygon_vertices(polygon)
                if len(polygon) < 3:
                    continue

                tex = face["texture"]

                # Skip faces the OG also skips (caulk / invisible surfaces)
                if tex.startswith("__") or tex == "TB_empty" or tex == "invisible":
                    continue

                # Compute UVs on Quake-space points BEFORE transform
                uvs = [compute_uv(v, face, scale) for v in polygon]

                # Transform vertices to game space
                game_verts = [transform_point(v, scale) for v in polygon]

                # Compute face normal in game space
                face_normal = transform_normal(face["normal"])

                # Fan triangulate
                tri_indices = fan_triangulate(game_verts)

                groups[tex].append({
                    "verts": game_verts,
                    "uvs": uvs,
                    "normal": face_normal,
                    "tri_indices": tri_indices,
                })

    return dict(groups)


def compute_smooth_normals(faces, weld_epsilon=0.001):
    """Compute smooth vertex normals for a list of faces.

    Args:
        faces: list of dicts with 'verts', 'tri_indices', 'normal'
        weld_epsilon: max distance for vertex welding

    Returns:
        list of vertex normals (one per vertex position)
    """
    # Collect all raw vertices
    all_positions = []
    for face in faces:
        all_positions.extend(face["verts"])
    n_verts = len(all_positions)

    # Init normals to zero
    norms = [(0.0, 0.0, 0.0) for _ in range(n_verts)]

    # Build vertex weld map: find positions within epsilon
    weld_map = list(range(n_verts))
    # Simple brute-force welding for small meshes
    for i in range(n_verts):
        if weld_map[i] != i:
            continue  # already welded
        xi, yi, zi = all_positions[i]
        for j in range(i + 1, n_verts):
            xj, yj, zj = all_positions[j]
            dx = xi - xj
            dy = yi - yj
            dz = zi - zj
            if dx*dx + dy*dy + dz*dz <= weld_epsilon * weld_epsilon:
                weld_map[j] = i

    # Accumulate face normals per welded vertex
    vert_start = 0
    for face in faces:
        verts = face["verts"]
        tri_indices = face["tri_indices"]
        nv = len(verts)

        for fi in range(nv):
            idx = vert_start + fi
            group = weld_map[idx]
            fn = face["normal"]
            norms[group] = (
                norms[group][0] + fn[0],
                norms[group][1] + fn[1],
                norms[group][2] + fn[2],
            )
        vert_start += nv

    # Normalize and expand back to original vertex order
    result = []
    for i in range(n_verts):
        group = weld_map[i]
        nx, ny, nz = norms[group]
        length = math.sqrt(nx*nx + ny*ny + nz*nz)
        if length > 1e-10:
            result.append((nx/length, ny/length, nz/length))
        else:
            result.append((0.0, 1.0, 0.0))  # default upward

    return result


# Per-material vertex colors (RGBA bytes) so surfaces are distinguishable
# even before sprite textures are wired up.
MATERIAL_COLORS = {
    "snow_1":               (235, 240, 250, 255),
    "rock_1":               (150, 135, 115, 255),
    "rock_2":               (110, 100,  85, 255),
    "rock_1_climbable":     (160, 145, 125, 255),
    "metal_floor_1":        (120, 130, 145, 255),
    "floor_dirty_concrete": ( 95,  90,  85, 255),
    "TB_empty":             ( 30,  35,  40, 255),
}
_DEFAULT_COLOR = (180, 180, 180, 255)


def build_glb_data(groups: dict[str, list]):
    """Build per-mesh arrays from grouped face data.

    Returns: list of dicts, each with:
        name, positions[], normals[], uvs[], colors[], indices[], material_idx
    """
    meshes = []
    materials = sorted(groups.keys())

    for mat_name in materials:
        faces = groups[mat_name]
        smooth_normals = compute_smooth_normals(faces)

        pos_data: list[float] = []
        nrm_data: list[float] = []
        uv_data: list[float] = []
        col_data: list[int] = []
        idx_data: list[int] = []
        mat_color = MATERIAL_COLORS.get(mat_name, _DEFAULT_COLOR)

        vert_offset = 0
        vi = 0  # global vertex index

        for face in faces:
            verts = face["verts"]
            uvs = face["uvs"]
            tri_indices = face["tri_indices"]

            for j in range(len(verts)):
                v = verts[j]
                n = smooth_normals[vi]
                uv = uvs[j]
                pos_data.extend([v[0], v[1], v[2]])
                nrm_data.extend([n[0], n[1], n[2]])
                uv_data.extend([uv[0], uv[1]])
                col_data.extend(mat_color)  # RGBA bytes per vertex
                vi += 1

            for i0, i1, i2 in tri_indices:
                idx_data.extend([
                    vert_offset + i0,
                    vert_offset + i1,
                    vert_offset + i2,
                ])

            vert_offset += len(verts)

        meshes.append({
            "name": mat_name,
            "positions": pos_data,
            "normals": nrm_data,
            "uvs": uv_data,
            "colors": col_data,
            "indices": idx_data,
            "material_idx": materials.index(mat_name),
        })

    return meshes, materials


# ── glTF 2.0 binary writer ─────────────────────────────────────────

def write_glb(path: str, meshes: list, materials: list[str]):
    """Write a glTF 2.0 binary (.glb) file.

    Uses stdlib only — no external dependencies.
    """
    # Build the binary buffer
    buffer_parts: list[bytes] = []
    mesh_views: list[dict] = []  # per-mesh: {pos_start, pos_len, nrm_start, uv_start, idx_start, idx_len}

    for mesh in meshes:
        pos = mesh["positions"]
        nrm = mesh["normals"]
        uvs = mesh["uvs"]
        col = mesh["colors"]
        idx = mesh["indices"]

        # Interleave vertex data: pos(3f) + nrm(3f) + uv(2f) + color(4B) = 36 bytes per vertex
        vert_bytes = bytearray()
        v_count = len(pos) // 3
        for i in range(v_count):
            vert_bytes.extend(struct.pack("<fff", pos[i*3], pos[i*3+1], pos[i*3+2]))
            vert_bytes.extend(struct.pack("<fff", nrm[i*3], nrm[i*3+1], nrm[i*3+2]))
            vert_bytes.extend(struct.pack("<ff", uvs[i*2], uvs[i*2+1]))
            vert_bytes.extend(struct.pack("<BBBB", col[i*4], col[i*4+1], col[i*4+2], col[i*4+3]))

        # Index bytes (uint16)
        idx_bytes = struct.pack(f"<{len(idx)}H", *idx)

        views = {
            "pos_start": sum(len(b) for b in buffer_parts),
        }
        buffer_parts.append(bytes(vert_bytes))
        views["pos_len"] = len(vert_bytes)

        # Index data goes after vertex data (must be aligned to 4 bytes)
        # But glTF allows separate buffer views, so we just append
        pad = (4 - len(vert_bytes) % 4) % 4
        if pad:
            buffer_parts.append(b'\x00' * pad)

        views["idx_start"] = sum(len(b) for b in buffer_parts)
        buffer_parts.append(bytes(idx_bytes))
        views["idx_len"] = len(idx_bytes)

        mesh_views.append(views)

    full_buffer = b''.join(buffer_parts)
    buffer_len = len(full_buffer)

    # Build glTF JSON
    json_doc = {
        "asset": {"version": "2.0", "generator": "bake_glb.py"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"mesh": i, "name": mat} for i, mat in enumerate(materials)],
        "meshes": [],
        "materials": [],
        "accessors": [],
        "bufferViews": [],
        "buffers": [{"byteLength": buffer_len}],
    }

    # Per-material default (no textures in glTF — handled at runtime)
    # Color palette: each texture name maps to a distinct base color
    # so the baked .t3dm has visual variety even without sprites.
    TEX_COLORS = {
        "snow_1":               [0.90, 0.92, 0.95, 1.0],
        "rock_1":               [0.45, 0.42, 0.38, 1.0],
        "rock_2":               [0.35, 0.32, 0.28, 1.0],
        "metal_floor_1":        [0.30, 0.35, 0.40, 1.0],
        "floor_dirty_concrete": [0.55, 0.53, 0.50, 1.0],
        "TB_empty":             [0.20, 0.25, 0.30, 1.0],  # dark placeholder
    }
    _default_base = [0.5, 0.5, 0.5, 1.0]

    for mat_name in materials:
        # mat_name is like "mat_rock_1" — strip "mat_" prefix to get texture name
        tex_name = mat_name[4:] if mat_name.startswith("mat_") else mat_name
        color = TEX_COLORS.get(tex_name, _default_base)
        json_doc["materials"].append({
            "name": mat_name,
            "pbrMetallicRoughness": {
                "baseColorFactor": color,
                "metallicFactor": 0.0,
                "roughnessFactor": 1.0,
            },
        })

    # Buffer views and accessors
    for i, (mesh, views) in enumerate(zip(meshes, mesh_views)):
        mat_idx = mesh["material_idx"]
        v_count = len(mesh["positions"]) // 3
        idx_count = len(mesh["indices"])

        # Vertex buffer view (interleaved POSITION + NORMAL + TEXCOORD_0 + COLOR_0)
        json_doc["bufferViews"].append({
            "buffer": 0,
            "byteOffset": views["pos_start"],
            "byteLength": views["pos_len"],
            "byteStride": 36,  # 3f+3f+2f+4B = 36 bytes per vertex
        })
        bv_vert = len(json_doc["bufferViews"]) - 1

        # Index buffer view
        json_doc["bufferViews"].append({
            "buffer": 0,
            "byteOffset": views["idx_start"],
            "byteLength": views["idx_len"],
        })
        bv_idx = len(json_doc["bufferViews"]) - 1

        # Position accessor
        json_doc["accessors"].append({
            "bufferView": bv_vert,
            "byteOffset": 0,
            "componentType": 5126,  # FLOAT
            "count": v_count,
            "type": "VEC3",
            "min": [
                min(mesh["positions"][i*3] for i in range(v_count)),
                min(mesh["positions"][i*3+1] for i in range(v_count)),
                min(mesh["positions"][i*3+2] for i in range(v_count)),
            ],
            "max": [
                max(mesh["positions"][i*3] for i in range(v_count)),
                max(mesh["positions"][i*3+1] for i in range(v_count)),
                max(mesh["positions"][i*3+2] for i in range(v_count)),
            ],
        })
        acc_pos = len(json_doc["accessors"]) - 1

        # Normal accessor (at byte offset 12 within vertex)
        json_doc["accessors"].append({
            "bufferView": bv_vert,
            "byteOffset": 12,
            "componentType": 5126,  # FLOAT
            "count": v_count,
            "type": "VEC3",
        })
        acc_nrm = len(json_doc["accessors"]) - 1

        # UV accessor (at byte offset 24 within vertex)
        json_doc["accessors"].append({
            "bufferView": bv_vert,
            "byteOffset": 24,
            "componentType": 5126,  # FLOAT
            "count": v_count,
            "type": "VEC2",
        })
        acc_uv = len(json_doc["accessors"]) - 1

        # Vertex color accessor (at byte offset 32 within vertex)
        json_doc["accessors"].append({
            "bufferView": bv_vert,
            "byteOffset": 32,
            "componentType": 5121,  # UNSIGNED_BYTE
            "normalized": True,
            "count": v_count,
            "type": "VEC4",
        })
        acc_col = len(json_doc["accessors"]) - 1

        # Index accessor
        json_doc["accessors"].append({
            "bufferView": bv_idx,
            "byteOffset": 0,
            "componentType": 5123,  # UNSIGNED_SHORT
            "count": idx_count,
            "type": "SCALAR",
        })
        acc_idx = len(json_doc["accessors"]) - 1

        # Mesh
        json_doc["meshes"].append({
            "name": mesh["name"],
            "primitives": [{
                "attributes": {
                    "POSITION": acc_pos,
                    "NORMAL": acc_nrm,
                    "TEXCOORD_0": acc_uv,
                    "COLOR_0": acc_col,
                },
                "indices": acc_idx,
                "material": mat_idx,
            }],
        })

    # Write binary glTF
    json_str = json.dumps(json_doc, separators=(",", ":"))
    json_bytes = json_str.encode("utf-8")

    # Pad JSON to 4-byte boundary
    json_pad = (4 - len(json_bytes) % 4) % 4
    json_bytes += b' ' * json_pad

    # Pad buffer to 4-byte boundary
    buffer_pad = (4 - buffer_len % 4) % 4
    if buffer_pad:
        full_buffer += b'\x00' * buffer_pad

    # Write file
    header = struct.pack("<IIII", 0x46546C67, 2, 12 + 8 + len(json_bytes) + 8 + len(full_buffer), 0)
    # magic = 0x46546C67 = "glTF"
    # version = 2
    # length = total file length

    with open(path, "wb") as f:
        # Header
        f.write(struct.pack("<I", 0x46546C67))  # magic: "glTF"
        f.write(struct.pack("<I", 2))  # version
        total_len = 12 + 8 + len(json_bytes) + 8 + len(full_buffer)
        f.write(struct.pack("<I", total_len))
        # JSON chunk
        f.write(struct.pack("<I", len(json_bytes)))
        f.write(b"JSON")
        f.write(json_bytes)
        # BIN chunk
        f.write(struct.pack("<I", len(full_buffer)))
        f.write(b"BIN\x00")
        f.write(full_buffer)


# ── Main ────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description="GLB baker — brush geometry → .glb")
    parser.add_argument("in_map", help="Input OG .map file")
    parser.add_argument("--out", required=True, help="Output .glb file")
    parser.add_argument("--scale", type=float, default=0.2, help="World scale")
    args = parser.parse_args()

    parsed_map = parse_map(args.in_map)

    groups = collect_faces(parsed_map, args.scale)
    if not groups:
        print("[glb] error: no geometry collected!", file=sys.stderr)
        sys.exit(1)

    meshes, materials = build_glb_data(groups)
    total_verts = sum(len(m["positions"]) // 3 for m in meshes)
    total_tris = sum(len(m["indices"]) // 3 for m in meshes)
    print(f"[glb] {len(materials)} materials, {total_verts} verts, {total_tris} tris")

    write_glb(args.out, meshes, materials)
    file_size = os.path.getsize(args.out)
    print(f"[glb] wrote {args.out} ({file_size} bytes)")


if __name__ == "__main__":
    main()
