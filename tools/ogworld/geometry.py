#!/usr/bin/env python3
"""World geometry builder (Inc 3).

Computes brush polygons once in the canonical IR, orients them from the
transformed face normal, deduplicates vertices, fan-triangulates, and attaches
source/material identity. The global collision list and duplicated visual room
inputs derive from these same polygons.
"""

from __future__ import annotations

import math
from dataclasses import replace
from typing import List, Tuple, Dict, Optional

from .model import (
    WorldBuild, WorldPolygon, CollisionTriangle, SourceFace,
    MAT_SOLID, MAT_ONEWAY, MAT_DEATH, MAT_CLIMBABLE,
)
from .class_policy import (
    COLLISION_SOLID, COLLISION_ONEWAY, COLLISION_CLIMBABLE, COLLISION_NONE,
    RENDER_STATIC, RENDER_ACTOR, RENDER_NONE,
    DYNAMIC_STATIC_PROXY, policy_for,
)

# Reuse the proven geometry primitives.
from ogmap_lib import transform_point, transform_normal
from ogmap_lib.brush_geom import (
    compute_face_polygon, sort_vertices_ccw, dedupe_polygon_vertices,
    fan_triangulate, outward_face_normal,
)
from ogmap_lib.texture_mapping import compute_uv


def _is_finite(v) -> bool:
    return all(math.isfinite(x) for x in v)


def _edge_cross(a, b, c):
    """Cross product of (b - a) x (c - a) for the first triangle edges."""
    return (
        (b[1] - a[1]) * (c[2] - a[2]) - (b[2] - a[2]) * (c[1] - a[1]),
        (b[2] - a[2]) * (c[0] - a[0]) - (b[0] - a[0]) * (c[2] - a[2]),
        (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]),
    )


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
    cross = _edge_cross(a, b, c)
    area2 = math.sqrt(cross[0] ** 2 + cross[1] ** 2 + cross[2] ** 2)
    if area2 < 1e-9:
        return "polygon is degenerate (zero area)"
    return None


def validate_triangle_normal(verts, normal) -> Optional[str]:
    """Return an error string if the triangle's winding disagrees with normal."""
    if len(verts) < 3:
        return "triangle has fewer than 3 vertices"
    a, b, c = verts[0], verts[1], verts[2]
    cross = _edge_cross(a, b, c)
    dot = cross[0] * normal[0] + cross[1] * normal[1] + cross[2] * normal[2]
    if dot < 0:
        return "triangle winding disagrees with face normal"
    return None


def _entity_solid_transforms(build: WorldBuild, scale: float):
    """Recreate OG GenerateSolid centering plus Actor origin/yaw at map load."""
    brushes_by_entity = {}
    for brush in build.source_brushes:
        policy = policy_for(brush.classname)
        if policy is not None and policy.dynamic == DYNAMIC_STATIC_PROXY:
            brushes_by_entity.setdefault(brush.entity_index, []).append(brush)

    transforms = {}
    for entity_index, brushes in brushes_by_entity.items():
        source_vertices = []
        for brush in brushes:
            planes = list(brush.planes)
            for face_index in range(len(planes)):
                source_vertices.extend(compute_face_polygon(planes, face_index))
        if not source_vertices:
            continue

        # Map.Load computes one union AABB over every child brush before
        # centering each face into the Solid's local space.
        world_vertices = [transform_point(v, scale) for v in source_vertices]
        center = tuple(
            (min(v[axis] for v in world_vertices)
             + max(v[axis] for v in world_vertices)) * 0.5
            for axis in range(3)
        )
        sample = brushes[0]
        position = (transform_point(sample.entity_origin, scale)
                    if sample.has_entity_origin else center)
        radians = math.radians(sample.entity_angle_degrees)
        transforms[entity_index] = (center, position, math.cos(radians), math.sin(radians))
    return transforms


def _apply_entity_solid_transform(point, normal, transform):
    center, position, cosine, sine = transform
    # OG rotates around source Z. In port XZ, source +Y maps to world -Z.
    local_x = point[0] - center[0]
    local_z = point[2] - center[2]
    rotated = (
        position[0] + cosine * local_x + sine * local_z,
        position[1] + point[1] - center[1],
        position[2] - sine * local_x + cosine * local_z,
    )
    rotated_normal = (
        cosine * normal[0] + sine * normal[2],
        normal[1],
        -sine * normal[0] + cosine * normal[2],
    )
    return rotated, rotated_normal


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
    entity_transforms = _entity_solid_transforms(build, scale)

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
            outward = outward_face_normal(planes[fi])
            polygon = sort_vertices_ccw(polygon, outward)
            polygon = dedupe_polygon_vertices(polygon)
            if len(polygon) < 3:
                diagnostics.append(f"face {ei}:{bi}:{fi} deduped to <3 verts")
                continue

            # Match GenerateSolid's authored local render vertices before the
            # final actor Position/Facing transform.
            game_verts = [transform_point(v, scale) for v in polygon]
            normal = transform_normal(outward)
            actor_local_verts: Tuple[Tuple[float, float, float], ...] = ()
            actor_local_normal = None
            entity_transform = entity_transforms.get(ei)
            if entity_transform is not None:
                center = entity_transform[0]
                actor_local_verts = tuple(
                    (point[0] - center[0], point[1] - center[1], point[2] - center[2])
                    for point in game_verts
                )
                actor_local_normal = normal
                transformed = [
                    _apply_entity_solid_transform(v, normal, entity_transform)
                    for v in game_verts
                ]
                game_verts = [item[0] for item in transformed]
                normal = transformed[0][1]

            err = validate_polygon(game_verts, normal)
            if err:
                diagnostics.append(f"face {ei}:{bi}:{fi}: {err}")
                continue

            # UVs computed on Quake-space points BEFORE transform.
            uvs = [compute_uv(v, planes[fi], scale,
                              (sf.uv_scale_u, sf.uv_scale_v)) for v in polygon]

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
                actor_local_verts=actor_local_verts,
                actor_local_normal=actor_local_normal,
            ))

    # The entity transform helper follows Map.LoadActor: generated brush AABB
    # center supplies initial Position unless an entity origin overrides it.
    resolved_solids = []
    for record in build.dynamic_solids:
        transform = entity_transforms.get(record.source_id)
        if transform is None:
            raise ValueError(f"dynamic solid {record.source_id} has no generated Solid transform")
        resolved_solids.append(replace(record, position=transform[1]))
    build.dynamic_solids = resolved_solids

    return polygons, diagnostics
