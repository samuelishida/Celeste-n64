#!/usr/bin/env python3
"""Global CMSH writer (Inc 4).

Writes one CMSH v2 from the global collision triangle stream, including
quantization, BVH construction from quantized positions, face/material ids,
and a runtime-query audit sidecar mapping triangle ids to source ids.

The expected current reference is approximately 20,824 vertices, 10,596
triangles, and 8,191 BVH nodes.
"""

import sys
import struct
import json
from pathlib import Path
from typing import List, Tuple, Optional

# Add tools to path for imports.
sys.path.insert(0, str(Path(__file__).parent.parent))

from ogworld.collision import CollisionScene
from ogworld.model import CollisionTriangle

# Reuse the proven quantization + BVH from the existing colmesh writer.
from writers.colmesh_writer import (
    quantize_all, build_bvh, BvhNode, INT16_MAX,
)

MAGIC = b"CMSH"
VERSION = 2
FLAG_HAS_BVH = 0x0001


def float_to_uint32(v: float) -> int:
    return struct.unpack(">I", struct.pack(">f", v))[0]


def source_face_id_map(scene: CollisionScene) -> dict[tuple[int, int, int], int]:
    """Return CMSH's canonical 1-based face ids, including OG tie order."""
    face_order = {}
    for t in scene.tris:
        key = (t.entity_index, t.brush_index, t.face_index)
        value = (t.og_solid_id, t.og_face_order)
        if key in face_order and face_order[key] != value:
            raise ValueError(f"inconsistent OG face order metadata for {key}")
        face_order[key] = value
    ordered_face_keys = sorted(
        face_order,
        key=lambda key: (
            0 if face_order[key][0] >= 0 else 1,
            face_order[key][0] if face_order[key][0] >= 0 else 0,
            face_order[key][1] if face_order[key][1] >= 0 else 0,
            key,
        ),
    )
    if len(ordered_face_keys) > 0xFFFF:
        raise ValueError("source face group count exceeds CMSH uint16 limit")
    return {key: index + 1 for index, key in enumerate(ordered_face_keys)}


def write_colmesh(
    scene: CollisionScene,
    out_path: str,
    audit_path: Optional[str] = None,
) -> dict:
    """Write one global CMSH from a CollisionScene.

    Returns a stats dict. `audit_path`, if given, receives a JSON sidecar
    mapping triangle ids to source face ids (host diagnostics only; it need
    not ship in the ROM).
    """
    verts = scene.verts
    tris = scene.tris

    if not tris:
        raise ValueError("no collision triangles to write")

    # Quantize vertices.
    qverts, origin, qscale = quantize_all(verts)

    # Build BVH from QUANTIZED positions.
    source_face_ids = source_face_id_map(scene)
    ordered_face_keys = sorted(source_face_ids, key=source_face_ids.__getitem__)
    face_order = {}
    for t in tris:
        face_order[(t.entity_index, t.brush_index, t.face_index)] = (
            t.og_solid_id, t.og_face_order)
    raw_tris = []
    for t in tris:
        key = (t.entity_index, t.brush_index, t.face_index)
        source_face_id = source_face_ids[key]
        raw_tris.append((t.i0, t.i1, t.i2, t.material_flags, source_face_id))
    bvh_nodes, flat_tris = build_bvh(raw_tris, qverts)
    stats = {
        "vertices": len(qverts),
        "triangles": len(flat_tris),
        "bvh_nodes": len(bvh_nodes),
        "og_solid_grid_records": len(scene.og_solid_grid),
    }

    face_ranges = {}
    for key in ordered_face_keys:
        solid_id = face_order[key][0]
        if solid_id < 0:
            continue
        face_id = source_face_ids[key]
        if solid_id not in face_ranges:
            face_ranges[solid_id] = [face_id, face_id]
        else:
            face_ranges[solid_id][1] = face_id
    solid_bounds = {record.solid_id: record for record in scene.og_solid_grid}
    if set(face_ranges) != set(solid_bounds):
        raise ValueError("OG solid bounds and collision face metadata disagree")
    if len(solid_bounds) > 0xFFFF:
        raise ValueError("OG SolidGrid record count exceeds uint16 limit")

    # Rebuild triangles in flat order, updating face_ids.
    reordered: List[Tuple[int, int, int, int, int, int]] = []
    for new_idx, raw in enumerate(flat_tris):
        i0, i1, i2, mat, source_face_id = raw
        reordered.append((i0, i1, i2, mat, new_idx, source_face_id))

    # Quantized AABB.
    qxs = [q[0] for q in qverts]
    qys = [q[1] for q in qverts]
    qzs = [q[2] for q in qverts]
    qaabb_min = (min(qxs), min(qys), min(qzs))
    qaabb_max = (max(qxs), max(qys), max(qzs))

    # 8-byte-aligned offsets.
    header_size = 72
    off_verts = header_size
    off_tris = (off_verts + len(qverts) * 6 + 7) & ~7
    off_bvh = (off_tris + len(reordered) * 12 + 7) & ~7
    off_slinks = (off_bvh + len(bvh_nodes) * 16 + 7) & ~7
    off_solid_grid = (off_slinks + 7) & ~7  # CMSH v2 starts records at 8-byte alignment

    has_bvh = 1 if bvh_nodes else 0

    with open(out_path, "wb") as f:
        f.write(MAGIC)
        f.write(struct.pack(">H", VERSION))
        f.write(struct.pack(">H", has_bvh))
        f.write(struct.pack(">hhhhhh",
            qaabb_min[0], qaabb_min[1], qaabb_min[2],
            qaabb_max[0], qaabb_max[1], qaabb_max[2]))
        f.write(struct.pack(">I", float_to_uint32(qscale)))
        f.write(struct.pack(">fff", origin[0], origin[1], origin[2]))
        f.write(struct.pack(">I", len(qverts)))
        f.write(struct.pack(">I", len(reordered)))
        f.write(struct.pack(">I", len(bvh_nodes)))
        f.write(struct.pack(">I", 0))  # surface_link_count
        f.write(struct.pack(">I", off_verts))
        f.write(struct.pack(">I", off_tris))
        f.write(struct.pack(">I", off_bvh))
        f.write(struct.pack(">I", off_slinks))
        f.write(struct.pack(">I", len(solid_bounds)))  # v2 solid-grid record count

        f.seek(off_verts)
        for qv in qverts:
            f.write(struct.pack(">hhh", qv[0], qv[1], qv[2]))

        f.seek(off_tris)
        for tri in reordered:
            i0, i1, i2, mat, fid, source_face_id = tri
            f.write(struct.pack(">HHHHHH", i0, i1, i2, mat & 0xFFFF,
                                fid & 0xFFFF, source_face_id & 0xFFFF))

        f.seek(off_bvh)
        for node in bvh_nodes:
            f.write(struct.pack(">hhhhhhHH",
                int(node.aabb_min[0]), int(node.aabb_min[1]), int(node.aabb_min[2]),
                int(node.aabb_max[0]), int(node.aabb_max[1]), int(node.aabb_max[2]),
                node.left_or_first & 0xFFFF, node.count_or_zero & 0xFFFF))

        f.seek(off_solid_grid)
        for solid_id in sorted(solid_bounds):
            record = solid_bounds[solid_id]
            first_face, last_face = face_ranges[solid_id]
            f.write(struct.pack(">HHHHffff",
                solid_id, first_face, last_face, 0,
                record.min_x, record.min_y, record.max_x, record.max_y))

    # Audit sidecar: triangle id -> source face id.
    if audit_path:
        # Build a lookup from (i0,i1,i2,mat) -> source face id. Each triangle
        # is unique by its vertex indices + material, so this is unambiguous.
        lookup = {}
        for t in scene.tris:
            key = (t.i0, t.i1, t.i2, t.material_flags)
            lookup[key] = f"{t.entity_index}:{t.brush_index}:{t.face_index}"
        audit = []
        for raw in flat_tris:
            key = (raw[0], raw[1], raw[2], raw[3])
            audit.append(lookup.get(key, "unknown"))
        with open(audit_path, "w") as f:
            json.dump({"triangle_source": audit}, f, indent=2)

    return stats
