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

DYNAMIC_SOLID_KIND = {
    "TrafficBlock": 1,
    "FallingBlock": 2,
    "FloatyBlock": 3,
    "MovingBlock": 4,
    "GateBlock": 5,
    "CassetteBlock": 6,
    "BreakBlock": 7,
    "DoubleDashPuzzleBlock": 8,
}
DYNAMIC_SOLID_MISSING_TARGET = 0xFFFFFFFF


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
    # Per-axis source->conditioned texture scale.  Kept on source identity so
    # clipped polygons recompute identical UVs in every visual cell.
    uv_scale_u: float = 1.0
    uv_scale_v: float = 1.0


@dataclass(frozen=True)
class SourceBrush:
    """One source brush with its identity and policy summary.

    `planes` is the list of face-plane dicts (as parsed by ogmap_lib) used to
    clip polygons. `faces` are the SourceFace records with identity + policy.
    """
    entity_index: int
    brush_index: int
    classname: str
    # OG Solid actors collect all brushes on an entity into one local solid,
    # then apply entity origin/yaw through Actor.Matrix at initial load.
    entity_origin: Vec3 = (0.0, 0.0, 0.0)
    has_entity_origin: bool = False
    entity_angle_degrees: int = 0
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
    # For actor-owned Solid faces, exact OG LocalVertices-space geometry
    # before the final actor Position/Facing transform.
    actor_local_verts: Tuple[Vec3, ...] = ()
    actor_local_normal: Optional[Vec3] = None


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
    og_solid_id: int = -1
    og_face_order: int = -1


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
class HazardRecord:
    """OG actor-backed hazard volume in world-space coordinates.

    Bounds reproduce Map.LoadActor's UseSolidsAsBounds + Actor.WorldBounds.
    Spike direction is its pre-SolidRayCast candidate; runtime applies OG's
    10-unit static-solid flip test against the loaded collision world.
    """
    kind: str                    # "death" | "spike"
    source_id: int
    bounds_min: Vec3
    bounds_max: Vec3
    spike_direction: Vec3 = (0.0, 0.0, 0.0)


@dataclass(frozen=True)
class DynamicSolidRecord:
    """Source-derived actor-owned Solid record for MPP5 + DSOL."""
    source_id: int
    classname: str
    kind: int
    position: Vec3 = (0.0, 0.0, 0.0)
    yaw_degrees: int = 0
    flags: int = 0
    target_source_id: int = DYNAMIC_SOLID_MISSING_TARGET
    target_position: Vec3 = (0.0, 0.0, 0.0)
    solid_id: int = 0xFFFF
    cms_first_source_face_id: int = 0
    cms_face_count: int = 0
    dsol_first_face: int = 0
    dsol_face_count: int = 0
    dsol_first_vertex: int = 0
    dsol_vertex_count: int = 0


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
    hazards: List[HazardRecord] = field(default_factory=list)
    dynamic_solids: List[DynamicSolidRecord] = field(default_factory=list)
    texture_manifest: List[str] = field(default_factory=list)
    texture_profiles: Dict[str, Dict[str, Any]] = field(default_factory=dict)
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
