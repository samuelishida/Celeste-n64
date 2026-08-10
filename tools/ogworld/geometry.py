#!/usr/bin/env python3
"""World geometry builder (Inc 3).

Computes brush polygons once in the canonical IR, orients them from the
transformed face normal, deduplicates vertices, fan-triangulates, and attaches
source/material identity. The global collision list and duplicated visual room
inputs derive from these same polygons.
"""

from __future__ import annotations

import math
from typing import List, Tuple, Dict, Optional

from .model import (
    WorldBuild, WorldPolygon, CollisionTriangle, SourceFace,
    MAT_SOLID, MAT_ONEWAY, MAT_DEATH, MAT_CLIMBABLE,
)
from .class_policy import (
    COLLISION_SOLID, COLLISION_ONEWAY, COLLISION_CLIMBABLE, COLLISION_NONE,
    RENDER_STATIC, RENDER_ACTOR, RENDER_NONE,
)

# Reuse the proven geometry primitives.
from ogmap_lib import transform_point, transform_normal
from ogmap_lib.brush_geom import (
    compute_face_polygon, sort_vertices_ccw, dedupe_polygon_vertices,
    fan_triangulate,
)
from ogmap_lib.texture_mapping import compute_uv


def _is_finite(v) -> bool:
    return all(math.isfinite(x) for x in v)


def validate_polygon(verts, normal) -> Optional[str]:
    """Return an error string if the polygon is degenerate/invalid."""
    if len(verts) < 3:
        return "polygon has fewer than 3 vertices"
    if not all(_is_finite(v) for v in verts):
        return "polygon has non-finite vertex"
    if not _is_finite(normal):
        return "polygon has non-finite normal"
    # Degenerate: zero area.
    a, b, c = verts[0], verts[1], verts[2]
    cross = (
        (b[1] - a[1]) * (c[2] - a[2]) - (b[2] - a[2]) * (c[1] - a[1]),
        (b[2] - a[2]) * (c[0] - a[0]) - (b[0] - a[0]) * (c[2] - a[2]),
        (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]),
    )
    area2 = math.sqrt(cross[0] ** 2 + cross[1] ** 2 + cross[2] ** 2)
    if area2 < 1e-9:
        return "polygon is degenerate (zero area)"
    return None


def validate_triangle_normal(verts, normal) -> Optional[str]:
    """Return an error string if the triangle's winding disagrees with normal."""
    if len(verts) < 3:
        return "triangle has fewer than 3 vertices"
    a, b, c = verts[0], verts[1], verts[2]
    cross = (
        (b[1] - a[1]) * (c[2] - a[2]) - (b[2] - a[2]) * (c[1] - a[1]),
        (b[2] - a[2]) * (c[0] - a[0]) - (b[0] - a[0]) * (c[2] - a[2]),
        (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]),
    )
    dot = cross[0] * normal[0] + cross[1] * normal[1] + cross[2] * normal[2]
    if dot < 0:
        return "triangle winding disagrees with face normal"
    return None


def build_world_geometry(
    build: WorldBuild,
    scale: float = 0.2,
    eps: float = 1e-4,
) -> Tuple[List[WorldPolygon], List[str]]:
    """Build all world polygons from the IR's source brushes.

    Returns (polygons, diagnostics). Each polygon is oriented from the
    transformed face normal, deduplicated, and carries source identity.

    This is the single source of truth for both global collision and visual
    room inputs.
    """
    polygons: List[WorldPolygon] = []
    diagnostics: List[str] = []

    for brush in build.source_brushes:
        ei, bi = brush.entity_index, brush.brush_index
        planes = list(brush.planes)
        for sf in brush.faces:
            fi = sf.face_index
            # Skip faces that contribute neither render nor collision.
            if sf.render_mode == RENDER_NONE and sf.collision_mode == COLLISION_NONE:
                continue

            # The face plane is planes[fi]; clip against all other planes.
            polygon = compute_face_polygon(planes, fi)
            if len(polygon) < 3:
                diagnostics.append(f"face {ei}:{bi}:{fi} produced empty polygon")
                continue
            polygon = sort_vertices_ccw(polygon, planes[fi]["normal"])
            polygon = dedupe_polygon_vertices(polygon)
            if len(polygon) < 3:
                diagnostics.append(f"face {ei}:{bi}:{fi} deduped to <3 verts")
                continue

            # Transform to world space.
            game_verts = [transform_point(v, scale) for v in polygon]
            normal = transform_normal(planes[fi]["normal"])

            err = validate_polygon(game_verts, normal)
            if err:
                diagnostics.append(f"face {ei}:{bi}:{fi}: {err}")
                continue

            # UVs computed on Quake-space points BEFORE transform.
            uvs = [compute_uv(v, planes[fi], scale) for v in polygon]

            # Material id from the texture manifest.
            try:
                mat_id = build.texture_manifest.index(sf.texture)
            except ValueError:
                mat_id = 0

            polygons.append(WorldPolygon(
                verts=tuple(game_verts),
                uvs=tuple(uvs),
                normal=normal,
                material_id=mat_id,
                material_flags=sf.material_flags,
                collision_mode=sf.collision_mode,
                render_mode=sf.render_mode,
                entity_index=ei,
                brush_index=bi,
                face_index=fi,
                classname=brush.classname,
                texture=sf.texture,
                src_face=sf,
            ))

    return polygons, diagnostics
