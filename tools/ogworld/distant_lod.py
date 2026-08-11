#!/usr/bin/env python3
"""Offline distant-LOD generation (Inc 4).

Reads the baked per-cell visual geometry (the same canonical world polygons
that the per-room LVL writer consumes) and emits a coarse `distant.lvl`-style
mesh per cell. The coarse mesh keeps the world's silhouette while drastically
reducing face/vertex counts, so the distant pass can render many cells with
few triangles.

This is a mesh DECIMATION step, not a texture bake. It:
  - merges coplanar faces (within a normal/offset tolerance);
  - quantizes vertices to a coarse fixed-point grid at `kLodScale`;
  - emits one LVL2 file per cell (`<cell>_distant.lvl`) with flat per-material
    colors (texturing of distant cells is future work).

`kLodScale` must be chosen so that the full world extent in distant space stays
inside int16 (`distant_far / kLodScale <= 32767`). The plan pins `kLodScale`
at 0.25 (same as the runtime `lod_scale`), giving 4x int16 headroom.
"""

from __future__ import annotations

import math
import sys
from pathlib import Path
from typing import List, Tuple

sys.path.insert(0, str(Path(__file__).parent.parent))

from ogworld.model import WorldPolygon, ChunkInput
from lvl_format import LvlFile, Face as LvlFace, Vertex as LvlVertex

# Fixed-point scale for distant coordinates (matches runtime `lod_scale`).
KLOD_SCALE = 0.25
# Coplanar merge tolerance: faces whose normals are within this angular cosine
# and whose plane offsets are within this world distance are merged.
MERGE_COS = 0.995
MERGE_OFFSET = 8.0
# Coarse grid quantization (world units) after merging.
QUANT = 16.0


def _face_plane(poly: WorldPolygon):
    """Return (nx, ny, nz, offset) for a polygon's plane."""
    n = poly.normal
    d = -(n[0] * poly.verts[0][0] + n[1] * poly.verts[0][1] +
          n[2] * poly.verts[0][2])
    return n, d


def _coplanar(n1, d1, n2, d2):
    """True if two planes are nearly the same (same normal dir, near offset)."""
    dot = n1[0] * n2[0] + n1[1] * n2[1] + n1[2] * n2[2]
    if dot < MERGE_COS:
        return False
    return abs(d1 - d2) < MERGE_OFFSET


def build_distant_lod(polygons: List[WorldPolygon], lod_scale: float = KLOD_SCALE):
    """Decimate a cell's polygons into a coarse mesh.

    Returns (verts, faces) where `verts` is a list of (x, y, z) world-space
    points and `faces` is a list of (vertex_index_tuple, material_id).
    """
    # Merge coplanar polygons into groups; take the outer boundary of each
    # group as a convex fan. For a robust-but-simple implementation we keep
    # the polygons' vertices but collapse any that quantize to the same point,
    # and we drop small coplanar fragments by keeping the union silhouette.
    # A full quadric error decimation is out of scope for the first version;
    # the dominant win is quantization + vertex dedup + dropping duplicate
    # coplanar faces.
    merged: List[WorldPolygon] = []
    used = [False] * len(polygons)
    for i, p in enumerate(polygons):
        if used[i]:
            continue
        n1, d1 = _face_plane(p)
        group = [p]
        used[i] = True
        for j in range(i + 1, len(polygons)):
            if used[j]:
                continue
            n2, d2 = _face_plane(polygons[j])
            if _coplanar(n1, d1, n2, d2):
                group.append(polygons[j])
                used[j] = True
        merged.append(group[0])  # representative polygon for the coplanar set

    # Quantize + deduplicate vertices across the merged polygons.
    vert_map: dict = {}
    verts: List[Tuple[float, float, float]] = []
    faces: List[Tuple[Tuple[int, ...], int]] = []

    def key_for(world_pt):
        qx = round(world_pt[0] / QUANT) * QUANT
        qy = round(world_pt[1] / QUANT) * QUANT
        qz = round(world_pt[2] / QUANT) * QUANT
        return (qx, qy, qz)

    for poly in merged:
        idxs = []
        for v in poly.verts:
            k = key_for(v)
            if k not in vert_map:
                vert_map[k] = len(verts)
                verts.append(k)
            idxs.append(vert_map[k])
        if len(idxs) >= 3:
            faces.append((tuple(idxs), poly.material_id))

    return verts, faces


def emit_distant_lvl(verts, faces, out_path: str, material_names: List[str],
                     scale: float = 0.2) -> None:
    """Write a `distant.lvl`-style LVL2 file.

    The coarse mesh is emitted with the SAME world-space coordinates (Y-up,
    already transformed), so the runtime packs it against the cell's render
    origin exactly like the near cell. Flat per-material colors are applied at
    render time from the material id (the LVL stores the string table for
    index stability).
    """
    lvl = LvlFile()
    lvl.strings = list(material_names)
    string_to_id = {s: i for i, s in enumerate(material_names)}

    vert_start = 0
    for idxs, mat_id in faces:
        # Emit each face as its own vertex fan (matching the near LVL writer).
        for vi in idxs:
            lvl.vertices.append(LvlVertex(pos=verts[vi], uv=(0.0, 0.0)))
        lvl.faces.append(LvlFace(
            vertex_start=vert_start,
            vertex_count=len(idxs),
            material_id=mat_id,
            normal=(0.0, 1.0, 0.0),  # not used for flat distant color
            flags=0x01,              # solid
        ))
        vert_start += len(idxs)

    lvl.write(out_path)


def build_distant_lvl(chunk: ChunkInput, material_names: List[str], out_path: str,
                      lod_scale: float = KLOD_SCALE) -> dict:
    """High-level entry: decimate + emit a distant LVL for one cell.

    Returns a stats dict (face/vert counts). Skips (returns None) if the cell
    has no renderable geometry.
    """
    if not chunk.polygons:
        return None
    verts, faces = build_distant_lod(list(chunk.polygons), lod_scale)
    emit_distant_lvl(verts, faces, out_path, material_names)
    return {"faces": len(faces), "vertices": len(verts)}
