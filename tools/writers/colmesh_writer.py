#!/usr/bin/env python3
"""Colmesh writer — generates .colmesh from ParsedMap.

Reads a ParsedMap from ogmap_lib, generates triangles with material flags
from the class registry, quantizes vertices, builds BVH from quantized
positions, and writes big-endian .colmesh binary.

This is extracted from bake_colmesh.py to be a shared writer module.
"""

import sys
import math
import struct
from pathlib import Path
from typing import List, Tuple, Optional

# Import library
sys.path.insert(0, str(Path(__file__).parent.parent))
from ogmap_lib import (
    ParsedMap, classify_entity, is_skipped,
    material_class_to_flags, is_upward_face, FaceFilter,
    MaterialClass, Vec3, ColmeshTriangle,
    compute_face_polygon, sort_vertices_ccw, dedupe_polygon_vertices,
    fan_triangulate, transform_point, transform_normal, Brush,
)

# ── Constants (matching colmesh_bake.py) ────────────────────────────

INT16_MAX = 32767
MAX_LEAF_TRIS = 4
MAX_DEPTH = 30
MAGIC = b"CMSH"
VERSION = 1
FLAG_HAS_BVH = 0x0001


class ColmeshStats:
    """Statistics from colmesh generation."""
    def __init__(self):
        self.vertices: int = 0
        self.triangles: int = 0
        self.bvh_nodes: int = 0
        self.skipped_brushes: int = 0


def generate_triangles(parsed_map: ParsedMap, scale: float) -> Tuple[List[Vec3], List[ColmeshTriangle]]:
    """Generate (world_verts, triangles) from brush entities.

    triangles is list of (i0,i1,i2,material_flags,face_id) where
    face_id == triangle array index (to be set during BVH build).
    """
    entities = parsed_map.entities
    all_verts: List[Vec3] = []
    all_tris: List[ColmeshTriangle] = []
    skipped = 0

    for ent in entities:
        cd = classify_entity(ent)
        if cd is None:
            if not is_skipped(ent) and ent.brushes:
                print(f"[colmesh] warn: unknown brush class '{ent.classname}'", file=sys.stderr)
            skipped += 1
            continue

        mat_class = cd.material_class
        if mat_class == MaterialClass.VISUAL_ONLY or mat_class == MaterialClass.TRIGGER:
            continue  # not in colmesh

        if not ent.brushes:
            continue

        material = material_class_to_flags(mat_class)
        for brush in ent.brushes:
            faces = brush.faces
            for face_idx, face in enumerate(faces):
                # Upward-only filter for DEATH surfaces
                if cd.face_filter == FaceFilter.UPWARD_ONLY and not is_upward_face(face):
                    continue

                # Skip caulk / invisible faces (matches OG behavior)
                tex = face["texture"]
                if tex.startswith("__") or tex == "TB_empty" or tex == "invisible":
                    continue

                polygon = compute_face_polygon(faces, face_idx)
                if len(polygon) < 3:
                    continue
                polygon = sort_vertices_ccw(polygon, face["normal"])
                polygon = dedupe_polygon_vertices(polygon)
                if len(polygon) < 3:
                    continue

                # Transform to game space
                game_verts = [transform_point(v, scale) for v in polygon]

                # Fan triangulate
                tri_indices = fan_triangulate(game_verts)

                # Add vertices
                base_idx = len(all_verts)
                all_verts.extend(game_verts)

                for i0_rel, i1_rel, i2_rel in tri_indices:
                    # face_id will be fixed after BVH reordering
                    all_tris.append((
                        base_idx + i0_rel,
                        base_idx + i1_rel,
                        base_idx + i2_rel,
                        material,
                        0,  # placeholder face_id
                    ))

    return all_verts, all_tris


# ── Quantization ────────────────────────────────────────────────────

def compute_quant(positions: List[Vec3]) -> Tuple[Vec3, float]:
    if not positions:
        return (0.0, 0.0, 0.0), 1.0
    xs = [p[0] for p in positions]
    ys = [p[1] for p in positions]
    zs = [p[2] for p in positions]
    origin = (min(xs), min(ys), min(zs))
    max_range = max(
        max(xs) - origin[0],
        max(ys) - origin[1],
        max(zs) - origin[2],
        1e-6
    )
    scale = max_range / INT16_MAX
    return origin, scale


def quantize(pos: Vec3, origin: Vec3, scale: float) -> Tuple[int, int, int]:
    x = int(round((pos[0] - origin[0]) / scale))
    y = int(round((pos[1] - origin[1]) / scale))
    z = int(round((pos[2] - origin[2]) / scale))
    def clamp(v): return max(-INT16_MAX, min(INT16_MAX, v))
    return (clamp(x), clamp(y), clamp(z))


def quantize_all(positions: List[Vec3]) -> Tuple[List[Tuple[int, int, int]], Vec3, float]:
    """Quantize all positions. Returns (qverts, origin, scale)."""
    origin, scale = compute_quant(positions)
    qverts = [quantize(p, origin, scale) for p in positions]
    return qverts, origin, scale


# ── BVH build from QUANTIZED positions ──────────────────────────────

class BvhNode:
    __slots__ = ('aabb_min', 'aabb_max', 'left_or_first', 'count_or_zero')
    def __init__(self):
        self.aabb_min = (INT16_MAX, INT16_MAX, INT16_MAX)
        self.aabb_max = (-INT16_MAX, -INT16_MAX, -INT16_MAX)
        self.left_or_first = 0
        self.count_or_zero = 0


def tri_centroid(tri, qverts):
    a, b, c = qverts[tri[0]], qverts[tri[1]], qverts[tri[2]]
    return ((a[0]+b[0]+c[0])/3.0, (a[1]+b[1]+c[1])/3.0, (a[2]+b[2]+c[2])/3.0)


def tri_aabb(tri, qverts):
    pts = [qverts[tri[0]], qverts[tri[1]], qverts[tri[2]]]
    mn = (min(p[0] for p in pts), min(p[1] for p in pts), min(p[2] for p in pts))
    mx = (max(p[0] for p in pts), max(p[1] for p in pts), max(p[2] for p in pts))
    return mn, mx


def union_aabb(a_min, a_max, b_min, b_max):
    return (
        (min(a_min[0], b_min[0]), min(a_min[1], b_min[1]), min(a_min[2], b_min[2])),
        (max(a_max[0], b_max[0]), max(a_max[1], b_max[1]), max(a_max[2], b_max[2]))
    )


def build_bvh(tris, qverts, depth=0, _flat=None):
    """Build BVH from QUANTIZED positions.

    Returns (node_list, flat_sorted_tris) in depth-first order.
    flat_sorted_tris has triangles reordered so leaf nodes reference
    contiguous ranges. The caller must rebuild with updated face_ids.
    """
    top_call = (_flat is None)
    if top_call:
        _flat = []

    node = BvhNode()

    # Compute AABB from quantized positions
    mn = (INT16_MAX, INT16_MAX, INT16_MAX)
    mx = (-INT16_MAX, -INT16_MAX, -INT16_MAX)
    for tri in tris:
        t_mn, t_mx = tri_aabb(tri, qverts)
        mn, mx = union_aabb(mn, mx, t_mn, t_mx)
    node.aabb_min = mn
    node.aabb_max = mx

    def make_leaf(leaf_tris):
        first = len(_flat)
        _flat.extend(leaf_tris)
        node.left_or_first = first
        node.count_or_zero = len(leaf_tris)
        return [node]

    if len(tris) <= MAX_LEAF_TRIS or depth >= MAX_DEPTH:
        return (make_leaf(tris), _flat) if top_call else make_leaf(tris)

    # Split on longest axis by median centroid
    ext = (mx[0]-mn[0], mx[1]-mn[1], mx[2]-mn[2])
    axis = ext.index(max(ext))
    centroids = [(tri_centroid(tri, qverts)[axis], tri) for tri in tris]
    centroids.sort(key=lambda x: x[0])
    mid = len(centroids) // 2
    left_tris  = [c[1] for c in centroids[:mid]]
    right_tris = [c[1] for c in centroids[mid:]]

    if not left_tris or not right_tris:
        return (make_leaf(tris), _flat) if top_call else make_leaf(tris)

    left_nodes  = build_bvh(left_tris,  qverts, depth+1, _flat)
    right_nodes = build_bvh(right_tris, qverts, depth+1, _flat)

    # Internal node: left_or_first = offset to right child
    node.left_or_first = len(left_nodes) + 1
    node.count_or_zero = 0

    nodes = [node] + left_nodes + right_nodes
    return (nodes, _flat) if top_call else nodes


def float_to_uint32(v: float) -> int:
    return struct.unpack(">I", struct.pack(">f", v))[0]


def write_colmesh(
    parsed_map: ParsedMap,
    out_path: str,
    scale: float = 0.2,
    eps: float = 1e-4,
    strict: bool = False
) -> ColmeshStats:
    """Write .colmesh file from ParsedMap.

    Args:
        parsed_map: ParsedMap to convert
        out_path: Output .colmesh file path
        scale: World scale factor
        eps: Tolerance for geometry operations
        strict: If True, fail on invalid brushes

    Returns:
        ColmeshStats with generation statistics
    """
    stats = ColmeshStats()

    # Generate triangles
    world_verts, raw_tris = generate_triangles(parsed_map, scale)
    stats.vertices = len(world_verts)
    stats.triangles = len(raw_tris)

    if not raw_tris:
        print("[colmesh] no triangles generated", file=sys.stderr)
        return stats

    # Quantize vertices
    qverts, origin, qscale = quantize_all(world_verts)

    # Build BVH from QUANTIZED positions (CRITICAL FIX)
    bvh_nodes, flat_tris = build_bvh(raw_tris, qverts)
    stats.bvh_nodes = len(bvh_nodes)

    # Rebuild triangle array in flat order, updating face_ids
    reordered_tris: List[ColmeshTriangle] = []
    for new_idx, raw_tri in enumerate(flat_tris):
        i0, i1, i2, mat, _ = raw_tri
        reordered_tris.append((i0, i1, i2, mat, new_idx))

    # Compute quantized AABB
    qxs = [q[0] for q in qverts]
    qys = [q[1] for q in qverts]
    qzs = [q[2] for q in qverts]
    qaabb_min = (min(qxs), min(qys), min(qzs))
    qaabb_max = (max(qxs), max(qys), max(qzs))

    # Compute 8-byte-aligned offsets
    header_size = 72
    off_verts = header_size
    off_tris = (off_verts + len(qverts) * 6 + 7) & ~7
    off_bvh = (off_tris + len(reordered_tris) * 12 + 7) & ~7
    off_slinks = (off_bvh + len(bvh_nodes) * 16 + 7) & ~7

    has_bvh = 1 if bvh_nodes else 0

    # Write binary file
    with open(out_path, 'wb') as f:
        # Header (72 bytes)
        f.write(MAGIC)  # 4 bytes
        f.write(struct.pack('>H', VERSION))  # 2 bytes
        f.write(struct.pack('>H', has_bvh))  # 2 bytes
        f.write(struct.pack('>hhhhhh',
            qaabb_min[0], qaabb_min[1], qaabb_min[2],
            qaabb_max[0], qaabb_max[1], qaabb_max[2]))  # 12 bytes
        f.write(struct.pack('>I', float_to_uint32(qscale)))  # 4 bytes
        f.write(struct.pack('>fff', origin[0], origin[1], origin[2]))  # 12 bytes
        f.write(struct.pack('>I', len(qverts)))  # 4 bytes
        f.write(struct.pack('>I', len(reordered_tris)))  # 4 bytes
        f.write(struct.pack('>I', len(bvh_nodes)))  # 4 bytes
        f.write(struct.pack('>I', 0))  # surface_link_count = 0
        f.write(struct.pack('>I', off_verts))  # 4 bytes
        f.write(struct.pack('>I', off_tris))  # 4 bytes
        f.write(struct.pack('>I', off_bvh))  # 4 bytes
        f.write(struct.pack('>I', off_slinks))  # 4 bytes
        f.write(struct.pack('>I', 0))  # pad

        # Vertices (6 bytes each: int16 x3)
        f.seek(off_verts)
        for qv in qverts:
            f.write(struct.pack('>hhh', qv[0], qv[1], qv[2]))

        # Triangles (12 bytes each: uint16 x6)
        f.seek(off_tris)
        for tri in reordered_tris:
            i0, i1, i2, mat, fid = tri
            f.write(struct.pack('>HHHHHH', i0, i1, i2, mat & 0xFFFF, fid & 0xFFFF, 0))

        # BVH nodes (16 bytes each)
        f.seek(off_bvh)
        for node in bvh_nodes:
            f.write(struct.pack('>hhhhhhHH',
                int(node.aabb_min[0]), int(node.aabb_min[1]), int(node.aabb_min[2]),
                int(node.aabb_max[0]), int(node.aabb_max[1]), int(node.aabb_max[2]),
                node.left_or_first & 0xFFFF, node.count_or_zero & 0xFFFF))

    return stats
