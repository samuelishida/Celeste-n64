#!/usr/bin/env python3
"""DLOD v2 binary writer (compressed distant LOD).

Writes the compact distant-LOD container that the runtime loads straight into
`LvlRoomRenderer` (no float32→int16 repack, no material sort). Big-endian,
matching the LVL2 convention.

DLOD v2 layout (big-endian):
  header (44 B):
    u32 magic   0x444C4F44 ("DLOD")
    u32 version 2
    u32 flags            (bit0 = per-direction)
    u32 direction_count  (1, or 4 from Inc 4)
    u32 face_count       (total across directions)
    u32 vert_count       (total across directions; = 3 × face_count)
    u32 material_count   (≤ manifest size)
    f32 origin_x/y/z     (SHARED map-center origin, world — Inc 3 / D2)
    u8  reserved[4]
  per-direction section (direction_count ×):
    u32 dir_face_count
    u32 dir_vert_count       (= 3 × dir_face_count)
    verts: dir_vert_count × s16 xyz     (packed (world - origin) * kLodScale;
                                        consecutive triples — face i uses
                                        verts[3i..3i+2])
    materials: dir_face_count × u8 material_id  (index into the shared manifest)

Vertices are packed relative to the SHARED map-center origin (Inc 3 / D2,
not the per-cell origin) at `kLodScale`, so the int16 headroom rule is
`map_diagonal * kLodScale <= ~28000`. Faces are contiguous vertex triples
grouped by material at bake time (sorted), so the runtime needs no sort and
no indexed-draw support.
"""

from __future__ import annotations

import struct
from pathlib import Path
from typing import List, Tuple

# DLOD magic + version.
DLOD_MAGIC = 0x444C4F44  # "DLOD"
# Version 2 (Inc 3 / D2): the header `origin` is now the SHARED map-center
# origin (all cells pack relative to it), not the per-cell render origin. The
# byte layout is otherwise unchanged. A stale v1 `.dlod` (per-cell origins)
# fails to parse at runtime (cell skipped, never misrendered).
DLOD_VERSION = 2
# Flags bit 0 = per-direction (direction_count > 1).
FLAG_PER_DIRECTION = 0x00000001

# Fixed-point scale for packed coordinates (matches runtime kLodScale).
KLOD_SCALE = 0.25

# Runtime caps (must match dlod_format.hpp / the bake's face budget).
MAX_FACES_PER_DIRECTION = 65535
MAX_VERTS_PER_DIRECTION = 32767


def _pack_vert(v, origin, scale):
    """Pack a world vertex to s16 relative to `origin` at `scale`."""
    return (int(round((v[0] - origin[0]) * scale)),
            int(round((v[1] - origin[1]) * scale)),
            int(round((v[2] - origin[2]) * scale)))


def dlod_bytes(directions: List[Tuple[List, List]], material_count: int,
               origin, scale: float = KLOD_SCALE,
               per_direction: bool = False) -> bytes:
    """Serialize a DLOD v2 buffer.

    `directions` is a list of `(verts, faces)` where `verts` is a list of
    world-space (x, y, z) points and `faces` is a list of
    `(vertex_index_tuple, material_id)` (triangles). `material_count` is the
    manifest size (material ids must be < material_count). `origin` is the
    cell render origin (world). Returns the raw big-endian bytes.
    """
    if not directions:
        raise ValueError("dlod_bytes: at least one direction required")
    if material_count <= 0:
        raise ValueError("dlod_bytes: material_count must be > 0")

    total_faces = 0
    for verts, faces in directions:
        if len(faces) > MAX_FACES_PER_DIRECTION:
            raise ValueError(
                f"dlod_bytes: {len(faces)} faces > cap "
                f"{MAX_FACES_PER_DIRECTION} in one direction")
        total_faces += len(faces)
    # Per spec, vert_count = 3 × face_count (consecutive per-face triples).
    total_verts = 3 * total_faces

    flags = FLAG_PER_DIRECTION if per_direction else 0
    direction_count = len(directions)

    out = bytearray()
    # Header (44 B).
    out += struct.pack(">IIIIIII", DLOD_MAGIC, DLOD_VERSION, flags,
                       direction_count, total_faces, total_verts,
                       material_count)
    out += struct.pack(">fff", origin[0], origin[1], origin[2])
    out += struct.pack(">4B", 0, 0, 0, 0)

    # Per-direction sections.
    for verts, faces in directions:
        dir_face_count = len(faces)
        # Per spec, dir_vert_count = 3 × dir_face_count (consecutive per-face
        # triples). The bake's `verts` list may hold dedup leftovers not
        # referenced by any face; we emit only the referenced triples.
        dir_vert_count = 3 * dir_face_count
        out += struct.pack(">II", dir_face_count, dir_vert_count)

        # Vertices: consecutive per-face triples. Face i uses verts[3i..3i+2].
        packed_verts = []
        for idx_tuple, _mat in faces:
            if len(idx_tuple) != 3:
                raise ValueError("dlod_bytes: faces must be triangles (3 idx)")
            for vi in idx_tuple:
                packed_verts.append(_pack_vert(verts[vi], origin, scale))
        if len(packed_verts) != dir_vert_count:
            raise ValueError(
                f"dlod_bytes: expected {dir_vert_count} packed verts, got "
                f"{len(packed_verts)} (faces must reference 3 distinct verts)")
        for (x, y, z) in packed_verts:
            out += struct.pack(">hhh", x, y, z)

        # Materials: one u8 per face.
        for _idx_tuple, mat in faces:
            if mat < 0 or mat >= material_count:
                raise ValueError(
                    f"dlod_bytes: material_id {mat} out of range "
                    f"[0, {material_count})")
            out += struct.pack(">B", mat)

    return bytes(out)


def write_dlod(directions: List[Tuple[List, List]], material_count: int,
               origin, out_path: str, scale: float = KLOD_SCALE,
               per_direction: bool = False) -> None:
    """Write a DLOD v2 file. See `dlod_bytes` for the argument contract."""
    data = dlod_bytes(directions, material_count, origin, scale, per_direction)
    Path(out_path).write_bytes(data)
