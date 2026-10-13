#!/usr/bin/env python3
"""Big-endian DSOL v1 format for actor-local dynamic-solid render geometry."""

from __future__ import annotations

import math
import struct
from dataclasses import dataclass
from pathlib import Path
from typing import Sequence

MAGIC = b"DSOL"
VERSION = 1
HEADER = struct.Struct(">4sHHII")
ACTOR = struct.Struct(">IHBB3ff4I")
FACE = struct.Struct(">IHHHHIHH")
VERTEX = struct.Struct(">8f")
MAX_ACTORS = 256
MAX_FACES = 8192
MAX_VERTICES = 32768


@dataclass(frozen=True)
class DsolActor:
    source_id: int
    solid_id: int
    kind: int
    flags: int
    position: tuple[float, float, float]
    yaw_radians: float
    first_face: int
    face_count: int
    first_vertex: int
    vertex_count: int


@dataclass(frozen=True)
class DsolFace:
    source_id: int
    brush_index: int
    face_index: int
    cms_source_face_id: int
    material_id: int
    first_vertex: int
    vertex_count: int
    flags: int = 0


@dataclass(frozen=True)
class DsolVertex:
    position: tuple[float, float, float]
    normal: tuple[float, float, float]
    uv: tuple[float, float]


def encode(actors: Sequence[DsolActor], faces: Sequence[DsolFace],
           vertices: Sequence[DsolVertex]) -> bytes:
    if len(actors) > MAX_ACTORS:
        raise ValueError(f"DSOL actor count exceeds runtime cap {MAX_ACTORS}")
    if len(faces) > MAX_FACES or len(vertices) > MAX_VERTICES:
        raise ValueError("DSOL geometry exceeds runtime capacity")
    out = bytearray(HEADER.pack(MAGIC, VERSION, len(actors), len(faces), len(vertices)))
    seen_sources: set[int] = set()
    seen_solids: set[int] = set()
    next_face = 0
    next_vertex = 0
    for actor in actors:
        if actor.source_id in seen_sources or actor.solid_id in seen_solids:
            raise ValueError("duplicate DSOL actor/source solid id")
        seen_sources.add(actor.source_id)
        seen_solids.add(actor.solid_id)
        if not (0 <= actor.source_id <= 0xFFFFFFFF and 0 <= actor.solid_id <= 0xFFFF
                and 1 <= actor.kind <= 8 and 0 <= actor.flags <= 0xFF):
            raise ValueError("invalid DSOL actor identity/kind/flags")
        if (actor.first_face + actor.face_count > len(faces)
                or actor.first_vertex + actor.vertex_count > len(vertices)):
            raise ValueError("DSOL actor geometry range exceeds arrays")
        if (actor.first_face != next_face or actor.face_count <= 0
                or actor.first_vertex != next_vertex or actor.vertex_count <= 0):
            raise ValueError("DSOL actor ranges must be positive and contiguous")
        next_face += actor.face_count
        next_vertex += actor.vertex_count
        values = (*actor.position, actor.yaw_radians)
        if not all(math.isfinite(value) for value in values):
            raise ValueError("DSOL actor transform is non-finite")
        out += ACTOR.pack(actor.source_id, actor.solid_id, actor.kind, actor.flags,
                          *actor.position, actor.yaw_radians, actor.first_face,
                          actor.face_count, actor.first_vertex, actor.vertex_count)
    for face in faces:
        if not (0 <= face.source_id <= 0xFFFFFFFF
                and 0 <= face.brush_index <= 0xFFFF
                and 0 <= face.face_index <= 0xFFFF
                and 0 <= face.cms_source_face_id <= 0xFFFF
                and 0 <= face.material_id <= 0xFFFF
                and 0 <= face.vertex_count <= 0xFFFF
                and face.first_vertex + face.vertex_count <= len(vertices)
                and 0 <= face.flags <= 0xFFFF):
            raise ValueError("invalid DSOL face or vertex range")
        if face.vertex_count < 3:
            raise ValueError("DSOL face has fewer than three vertices")
    for actor in actors:
        actor_faces = faces[actor.first_face:actor.first_face + actor.face_count]
        for face in actor_faces:
            if (face.source_id != actor.source_id
                    or face.first_vertex < actor.first_vertex
                    or face.first_vertex + face.vertex_count >
                       actor.first_vertex + actor.vertex_count):
                raise ValueError("DSOL face escapes its owning actor range")
    if next_face != len(faces) or next_vertex != len(vertices):
        raise ValueError("DSOL actor ranges do not cover geometry arrays")
    for face in faces:
        out += FACE.pack(face.source_id, face.brush_index, face.face_index,
                         face.cms_source_face_id, face.material_id,
                         face.first_vertex, face.vertex_count, face.flags)
    for vertex in vertices:
        values = (*vertex.position, *vertex.normal, *vertex.uv)
        if not all(math.isfinite(value) for value in values):
            raise ValueError("DSOL vertex contains non-finite values")
        out += VERTEX.pack(*values)
    return bytes(out)


def decode(data: bytes) -> tuple[list[DsolActor], list[DsolFace], list[DsolVertex]]:
    if len(data) < HEADER.size:
        raise ValueError("truncated DSOL header")
    magic, version, actor_count, face_count, vertex_count = HEADER.unpack_from(data)
    expected = HEADER.size + actor_count * ACTOR.size + face_count * FACE.size + vertex_count * VERTEX.size
    if magic != MAGIC or version != VERSION:
        raise ValueError("unsupported DSOL magic/version")
    if expected != len(data):
        raise ValueError("truncated or trailing DSOL data")
    off = HEADER.size
    actors = []
    for _ in range(actor_count):
        row = ACTOR.unpack_from(data, off); off += ACTOR.size
        actors.append(DsolActor(row[0], row[1], row[2], row[3], tuple(row[4:7]),
                                row[7], *row[8:12]))
    faces = []
    for _ in range(face_count):
        row = FACE.unpack_from(data, off); off += FACE.size
        faces.append(DsolFace(*row))
    vertices = []
    for _ in range(vertex_count):
        row = VERTEX.unpack_from(data, off); off += VERTEX.size
        vertices.append(DsolVertex(tuple(row[0:3]), tuple(row[3:6]), tuple(row[6:8])))
    # Reuse the encoder's structural and finite-value validation.
    encode(actors, faces, vertices)
    return actors, faces, vertices


def read(path: str | Path) -> tuple[list[DsolActor], list[DsolFace], list[DsolVertex]]:
    return decode(Path(path).read_bytes())
