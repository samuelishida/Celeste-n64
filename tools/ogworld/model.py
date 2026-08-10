#!/usr/bin/env python3
"""Canonical OG world IR (Inc 2).

Immutable records describing the parsed source map in a deterministic,
source-identity-preserving form. Every emitted polygon, collision triangle,
actor spawn, and skipped class carries a source identity and explicit policy.

World positions are `(x, y, z)` floats with Y-up (post-transform). Cell keys
are `(ix, iz)` in world XZ. Source coordinates remain available for
diagnostics.

The IR preserves the writer inputs that the current pipeline recomputes later:
UVs, texture-manifest/material ordering, serialized entity properties, and the
exact classname/entity representation used by LVL2.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import List, Tuple, Dict, Optional, Any

# World-space position (Y-up).
Vec3 = Tuple[float, float, float]
# World-XZ grid cell key.
CellKey = Tuple[int, int]

# Material flag bits (matching coll_mesh.hpp).
MAT_SOLID = 0x0001
MAT_ONEWAY = 0x0002
MAT_DEATH = 0x0004
MAT_CLIMBABLE = 0x0008
MAT_ICE = 0x0010


@dataclass(frozen=True)
class SourceFace:
    """One source brush face with its identity and policy.

    `normal` is the transformed (world-space, Y-up) face normal.
    `material_flags` is the collision material bitmask.
    `collision_mode` / `render_mode` are the class policy modes.
    """
    entity_index: int
    brush_index: int
    face_index: int
    classname: str
    texture: str
    normal: Vec3
    material_flags: int
    collision_mode: str      # "solid" | "oneway" | "climbable" | "none"
    render_mode: str         # "static" | "actor" | "none"
    # Quake-space source points (for diagnostics / UV recompute).
    src_p1: Vec3
    src_p2: Vec3
    src_p3: Vec3
    shift_u: float = 0.0
    shift_v: float = 0.0
    rotation: float = 0.0
    scale_u: float = 1.0
    scale_v: float = 1.0


@dataclass(frozen=True)
class SourceBrush:
    """One source brush with its identity and policy summary.

    `planes` is the list of face-plane dicts (as parsed by ogmap_lib) used to
    clip polygons. `faces` are the SourceFace records with identity + policy.
    """
    entity_index: int
    brush_index: int
    classname: str
    planes: Tuple[dict, ...] = ()
    faces: Tuple[SourceFace, ...] = ()
    valid: bool = True
    diagnostics: Tuple[str, ...] = ()


@dataclass(frozen=True)
class WorldPolygon:
    """A transformed, oriented, deduplicated convex polygon.

    `verts` are world-space (Y-up) vertices in CCW order around `normal`.
    `uvs` are the per-vertex UVs (computed on Quake-space points).
    `material_id` is the index into the texture manifest.
    `material_flags` is the collision material bitmask.
    `src_face` is the source face record (for UV recompute on clipping).
    """
    verts: Tuple[Vec3, ...]
    uvs: Tuple[Tuple[float, float], ...]
    normal: Vec3
    material_id: int
    material_flags: int
    collision_mode: str
    render_mode: str
    entity_index: int
    brush_index: int
    face_index: int
    classname: str
    texture: str
    src_face: Optional["SourceFace"] = None


@dataclass(frozen=True)
class CollisionTriangle:
    """One global collision triangle with source identity.

    `i0, i1, i2` index into a shared world-vertex list.
    `material_flags` is the collision material bitmask.
    """
    i0: int
    i1: int
    i2: int
    material_flags: int
    entity_index: int
    brush_index: int
    face_index: int


@dataclass(frozen=True)
class SpawnRecord:
    """A stable spawn/checkpoint record (manifest data, not LVL order).

    `kind` is "start" | "anchor" | "actor".
    `source_id` is the source entity index.
    `room_id` is the owning visual cell id ("" if not yet assigned).
    `name` is the OG entity's `name` property (e.g. "Start").
    `classname` is the source entity classname.
    """
    kind: str
    source_id: int
    room_id: str
    position: Vec3
    name: str
    classname: str
    properties: Dict[str, str] = field(default_factory=dict)


@dataclass(frozen=True)
class ChunkInput:
    """One visual room's input: duplicated visual polygons + point entities.

    `cell` is the world-XZ cell key.
    `polygons` are the visual polygons assigned to this cell (a brush may
    appear in every cell whose column intersects its AABB).
    `spawns` are the point-entity spawn records whose origin is in this cell.
    """
    cell: CellKey
    polygons: Tuple[WorldPolygon, ...] = ()
    spawns: Tuple[SpawnRecord, ...] = ()


@dataclass
class WorldBuild:
    """The complete canonical world IR.

    `world_verts` is the shared world-space vertex list for global collision.
    `collision_tris` are the global collision triangles.
    `chunks` are the visual room inputs (keyed by cell).
    `spawns` are all stable spawn records.
    `texture_manifest` is the ordered texture list (material ids index it).
    `source_sha256` is the source map content hash.
    `scale` is the world scale factor.
    `chunk_size` is the visual cell size in MAP units.
    """
    world_verts: List[Vec3] = field(default_factory=list)
    collision_tris: List[CollisionTriangle] = field(default_factory=list)
    chunks: Dict[CellKey, ChunkInput] = field(default_factory=dict)
    spawns: List[SpawnRecord] = field(default_factory=list)
    texture_manifest: List[str] = field(default_factory=list)
    source_sha256: str = ""
    source_path: str = ""
    scale: float = 0.2
    chunk_size: float = 1000.0
    # Source brushes (with planes) for geometry rebuild.
    source_brushes: List[SourceBrush] = field(default_factory=list)
    # Per-source-brush -> cell duplication count (for the report).
    brush_cell_counts: Dict[Tuple[int, int], int] = field(default_factory=dict)
    # Class policy summary (classname -> policy dict).
    policy_summary: Dict[str, Dict[str, Any]] = field(default_factory=dict)
