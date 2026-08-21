#!/usr/bin/env python3
"""DLOD v2 binary writer (compressed distant LOD).

Writes the compact distant-LOD container that the runtime loads straight into
`LvlRoomRenderer` (no float32→int16 repack, no material sort). Big-endian,
matching the LVL2 convention.

DLOD v3 layout (big-endian):
  header (44 B):
    u32 magic   0x444C4F44 ("DLOD")
    u32 version 3
    u32 flags            (bit0 = per-direction, bit1 = has vertex colors)
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
    u8 color_flag                  (1 if per-face colors follow, else 0)
    colors: dir_face_count × u8     (per-face color index; present iff flag=1)

Vertices are packed relative to the SHARED map-center origin (Inc 3 / D2,
not the per-cell origin) at `kLodScale`, so the int16 headroom rule is
`map_diagonal * kLodScale <= ~28000`. Faces are contiguous vertex triples
grouped by material at bake time (sorted), so the runtime needs no sort and
no indexed-draw support.

Per-face colors (v3) let the distant pass tint each run by its material so a
building that straddles the near/distant handoff renders as one continuous
surface instead of a flat-white band. The color channel is optional: omitting
`vertex_colors` emits `color_flag = 0` and the v2 byte layout still parses, so
old committed `.dlod` files keep loading on the flat path.
"""

from __future__ import annotations

import struct
from pathlib import Path
from typing import List, Tuple

# DLOD magic + version.
DLOD_MAGIC = 0x444C4F44  # "DLOD"
# Version 3 (mottled-building-fix): the distant pass now carries per-face
# material colors as a vertex-color channel, so tall buildings that straddle the
# near/distant handoff share one palette instead of showing a flat-white band.
# The header `origin` is still the SHARED map-center origin; the byte layout is
# otherwise v2 with two additions: (a) a per-direction `color_flag` u8 emitted
# after the verts, and (b) per-direction per-face color bytes appended after the
# materials. A stale v1 `.dlod` (per-cell origins) fails to parse at runtime
# (cell skipped, never misrendered); a v2 `.dlod` still parses with
# `has_vertex_colors == false` (flat path).
DLOD_VERSION = 3
# Flags bit 0 = per-direction (direction_count > 1).
FLAG_PER_DIRECTION = 0x00000001
# Flags bit 1 = the container carries per-face vertex colors. Set in the header
# for discoverability before any direction's section is parsed; gated together
# with version >= 3 at load time (see dlod_format.hpp). This bit is optional:
# when color_flag stays 0 the v2 byte layout still parses.
FLAG_VERTEX_COLORS = 0x00000002

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
               per_direction: bool = False,
               vertex_colors: List[int] | None = None) -> bytes:
    """Serialize a DLOD v2/v3 buffer.

    `directions` is a list of `(verts, faces)` where `verts` is a list of
    world-space (x, y, z) points and `faces` is a list of
    `(vertex_index_tuple, material_id)` (triangles). `material_count` is the
    manifest size (material ids must be < material_count). `origin` is the
    cell render origin (world). `vertex_colors` is an optional flat list with
    one color index per face across ALL directions (in direction-major order);
    when provided it emits a per-direction `color_flag = 1` and appends the
    per-face color bytes, producing a v3 layout. Omitting it keeps the v2 byte
    layout (`color_flag = 0`). Returns the raw big-endian bytes.
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

    has_colors = vertex_colors is not None
    if has_colors and len(vertex_colors) != total_faces:
        raise ValueError(
            f"dlod_bytes: {len(vertex_colors)} vertex colors but "
            f"{total_faces} faces expected (one color per face)")

    flags = FLAG_PER_DIRECTION if per_direction else 0
    if has_colors:
        flags |= FLAG_VERTEX_COLORS
    direction_count = len(directions)

    out = bytearray()
    # Header (44 B).
    out += struct.pack(">IIIIIII", DLOD_MAGIC, DLOD_VERSION, flags,
                       direction_count, total_faces, total_verts,
                       material_count)
    out += struct.pack(">fff", origin[0], origin[1], origin[2])
    out += struct.pack(">4B", 0, 0, 0, 0)

    # Flat `vertex_colors` is direction-major; track the current direction's
    # offset into it as we append per-face color bytes.
    color_offset = 0

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

        # Vertex colors (v3 only): one u8 per face, emitted after materials.
        if has_colors:
            color_flag = 1
        else:
            color_flag = 0
        out += struct.pack(">B", color_flag)
        if has_colors:
            for ci in range(color_offset, color_offset + dir_face_count):
                idx = vertex_colors[ci]
                if idx < 0 or idx >= material_count:
                    raise ValueError(
                        f"dlod_bytes: vertex color index {idx} out of range "
                        f"[0, {material_count})")
                out += struct.pack(">B", idx)
            color_offset += dir_face_count

    return bytes(out)


def write_dlod(directions: List[Tuple[List, List]], material_count: int,
               origin, out_path: str, scale: float = KLOD_SCALE,
               per_direction: bool = False,
               vertex_colors: List[int] | None = None) -> None:
    """Write a DLOD v2/v3 file. See `dlod_bytes` for the argument contract."""
    data = dlod_bytes(directions, material_count, origin, scale, per_direction,
                      vertex_colors)
    Path(out_path).write_bytes(data)
