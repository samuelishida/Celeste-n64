#!/usr/bin/env python3
"""Global collision scene builder (Inc 3).

Collects all policy-approved static collision surfaces into one global
`CollisionScene`, preserving source identity and transformed normals. Does NOT
split by visual room or assign collision by brush center.
"""

from __future__ import annotations

import math
from typing import List, Tuple, Dict, Optional

from .model import (
    WorldBuild, WorldPolygon, CollisionTriangle,
    MAT_SOLID, MAT_ONEWAY, MAT_DEATH, MAT_CLIMBABLE,
)
from .class_policy import (
    COLLISION_SOLID, COLLISION_ONEWAY, COLLISION_CLIMBABLE, COLLISION_NONE,
)
from .geometry import validate_triangle_normal

# Reuse the proven fan triangulation.
from ogmap_lib.brush_geom import fan_triangulate


class CollisionScene:
    """One global collision scene: shared world verts + triangles."""

    def __init__(self):
        self.verts: List[Tuple[float, float, float]] = []
        self.tris: List[CollisionTriangle] = []
        # triangle index -> source face id string (for the audit sidecar).
        self.tri_source: List[str] = []

    def add_polygon(self, poly: WorldPolygon) -> None:
        """Add a polygon's collision triangles to the scene."""
        if poly.collision_mode == COLLISION_NONE:
            return
        base = len(self.verts)
        self.verts.extend(poly.verts)
        for i0, i1, i2 in fan_triangulate(poly.verts):
            self.tris.append(CollisionTriangle(
                i0=base + i0,
                i1=base + i1,
                i2=base + i2,
                material_flags=poly.material_flags,
                entity_index=poly.entity_index,
                brush_index=poly.brush_index,
                face_index=poly.face_index,
            ))
            self.tri_source.append(
                f"{poly.entity_index}:{poly.brush_index}:{poly.face_index}"
            )


def build_global_collision(
    polygons: List[WorldPolygon],
    scale: float = 0.2,
    eps: float = 1e-4,
) -> CollisionScene:
    """Build the global collision scene from all world polygons.

    Only polygons with a non-NONE collision mode contribute. Each polygon's
    triangles are added exactly once (no per-room duplication).
    """
    scene = CollisionScene()
    for poly in polygons:
        if poly.collision_mode == COLLISION_NONE:
            continue
        scene.add_polygon(poly)
    return scene


def collision_budget(scene: CollisionScene) -> Dict[str, float]:
    """Estimate the on-disk and resident memory budget for the global mesh.

    Returns a dict with vertex/triangle/BVH counts and byte estimates. The
    actual N64 resident allocation is measured in Inc 10.
    """
    n_verts = len(scene.verts)
    n_tris = len(scene.tris)
    # BVH node count is data-dependent; estimate ~0.77 * tris (matches the
    # measured 8191 nodes / 10596 tris ≈ 0.77).
    n_bvh = int(n_tris * 0.77)
    # On-disk: header(72) + verts*6 + tris*12 + bvh*16 (quantized int16).
    disk = 72 + n_verts * 6 + n_tris * 12 + n_bvh * 16
    # Resident: pre-dequantized verts (3xf32) + tris (3x u16 + u16 + u16) +
    # BVH nodes (6x i16 + 2x u16) + loader overhead.
    resident = (n_verts * 12 + n_tris * 8 + n_bvh * 16 + 4096)
    return {
        "vertices": n_verts,
        "triangles": n_tris,
        "bvh_nodes_est": n_bvh,
        "disk_bytes_est": disk,
        "resident_bytes_est": resident,
    }


def validate_global_coverage(
    scene: CollisionScene,
    polygons: List[WorldPolygon],
) -> List[str]:
    """Validate that every policy-approved solid polygon is in the scene.

    Returns a list of error strings. A solid polygon missing from the scene
    is a coverage failure.
    """
    errors: List[str] = []
    # Count solid polygons by source face.
    solid_faces = {
        (p.entity_index, p.brush_index, p.face_index)
        for p in polygons if p.collision_mode != COLLISION_NONE
    }
    # Count triangles in the scene by source face.
    scene_faces = {
        (t.entity_index, t.brush_index, t.face_index)
        for t in scene.tris
    }
    missing = solid_faces - scene_faces
    for m in sorted(missing):
        errors.append(f"solid face {m[0]}:{m[1]}:{m[2]} missing from global collision")
    return errors
