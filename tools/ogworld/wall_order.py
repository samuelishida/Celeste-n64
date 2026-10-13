"""Reconstruct OG SolidGrid insertion order for wall queries.

Map.Load combines worldspawn brushes into 1000-unit source-space chunks,
inserts chunks in X/Y/Z loop order, and appends brushes in reverse source
order. This module preserves that ordering plus each combined solid's XY
bounds for runtime SolidGrid.Query tie ordering. Entity-owned Solid actors
follow worldspawn chunks in map entity order.
"""

from __future__ import annotations

import math
from dataclasses import dataclass
from typing import Dict, Iterable, List, Tuple

from .class_policy import COLLISION_NONE, DYNAMIC_STATIC_PROXY, policy_for
from .model import WorldPolygon


@dataclass(frozen=True)
class SolidGridRecord:
    solid_id: int
    min_x: float
    min_y: float
    max_x: float
    max_y: float


def static_world_wall_order(
    polygons: Iterable[WorldPolygon], scale: float,
) -> Tuple[Dict[Tuple[int, int, int], Tuple[int, int]], List[SolidGridRecord], set]:
    """Return face-key -> (OG SolidGrid insertion ordinal, face ordinal), and bounds.

    Worldspawn uses Map.Load's 1000-unit chunks. Solid actors follow those
    chunks in entity order and collect all entity brushes into one Solid.
    """
    if scale <= 0:
        raise ValueError("scale must be positive")

    all_input_polys = list(polygons)
    # Map.QueryObjects recursively collects func_group child brushes into the
    # same staticSolids list as direct worldspawn brushes.
    all_polys = [p for p in all_input_polys
                 if p.classname in ("worldspawn", "func_group")]
    by_brush: Dict[Tuple[int, int], List[WorldPolygon]] = {}
    for poly in all_polys:
        by_brush.setdefault((poly.entity_index, poly.brush_index), []).append(poly)

    def source_point(v):
        # Inverse of transform_point((x,y,z), scale) -> (x,z,-y).
        return (v[0] / scale, -v[2] / scale, v[1] / scale)

    brush_bounds = {}
    for key, faces in by_brush.items():
        points = [source_point(v) for poly in faces for v in poly.verts]
        brush_bounds[key] = tuple(
            (min(p[i] for p in points), max(p[i] for p in points))
            for i in range(3)
        )

    mins = (tuple(min(bounds[i][0] for bounds in brush_bounds.values()) for i in range(3))
            if brush_bounds else None)
    maxs = (tuple(max(bounds[i][1] for bounds in brush_bounds.values()) for i in range(3))
            if brush_bounds else None)
    sizes = tuple(maxs[i] - mins[i] for i in range(3)) if brush_bounds else (0.0, 0.0, 0.0)
    # C# loop compares integer induction variable directly with the float
    # quotient (`x < bounds.Size.X / 1000`), so positive fractional counts
    # execute ceil(size / 1000) iterations.
    chunk_counts = tuple(math.ceil(size / 1000.0) for size in sizes)

    # Match Map.Load: strict loop bounds, inclusive box containment, reverse
    # scan/removal from the source-order available list.
    source_brushes = list(by_brush)
    assigned: Dict[Tuple[int, int], int] = {}
    records: List[SolidGridRecord] = []
    chunk_ordinal = 0
    for ix in range(chunk_counts[0]):
        for iy in range(chunk_counts[1]):
            for iz in range(chunk_counts[2]):
                upper = tuple(mins[a] + 1000.0 * (1 + (ix, iy, iz)[a]) for a in range(3))
                combined = []
                for key in reversed(source_brushes):
                    if key in assigned:
                        continue
                    bounds = brush_bounds[key]
                    center = tuple((bounds[a][0] + bounds[a][1]) * 0.5 for a in range(3))
                    if all(mins[a] <= center[a] <= upper[a] for a in range(3)):
                        assigned[key] = chunk_ordinal
                        combined.append(key)

                collision_faces = [
                    poly for key in combined for poly in by_brush[key]
                    if poly.collision_mode != COLLISION_NONE
                    and not poly.texture.startswith("__")
                    and poly.texture != "TB_empty"
                ]
                if collision_faces:
                    points = [v for poly in collision_faces for v in poly.verts]
                    xs = [p[0] for p in points]
                    ys = [-p[2] for p in points]  # OG SolidGrid uses source XY.
                    records.append(SolidGridRecord(
                        solid_id=chunk_ordinal,
                        min_x=min(xs), min_y=min(ys),
                        max_x=max(xs), max_y=max(ys),
                    ))
                chunk_ordinal += 1

    face_order: Dict[Tuple[int, int, int], Tuple[int, int]] = {}
    next_face_order: Dict[int, int] = {}
    for key, solid_id in assigned.items():
        ordered_faces = sorted(by_brush[key], key=lambda p: p.face_index)
        # Grouped solid's WorldFaces follows reversed brush order, then source
        # face order. Ignore faces without emitted collision; gaps do not alter
        # relative order of returned hits.
        for poly in ordered_faces:
            if (poly.collision_mode != COLLISION_NONE
                    and not poly.texture.startswith("__")
                    and poly.texture != "TB_empty"):
                face_order[(poly.entity_index, poly.brush_index, poly.face_index)] = (
                    solid_id, next_face_order.get(solid_id, 0),
                )
                next_face_order[solid_id] = next_face_order.get(solid_id, 0) + 1

    # Map.Load inserts entity actors after all worldspawn chunk solids. OG's
    # actor loop preserves entity order; CollectSolids preserves child order.
    actor_brushes: Dict[int, List[WorldPolygon]] = {}
    for poly in all_input_polys:
        policy = policy_for(poly.classname)
        if policy is not None and policy.dynamic == DYNAMIC_STATIC_PROXY:
            actor_brushes.setdefault(poly.entity_index, []).append(poly)
    next_solid_id = chunk_ordinal
    actor_records = []
    for entity_index in sorted(actor_brushes):
        solid_id = next_solid_id
        next_solid_id += 1
        actor_polys = sorted(actor_brushes[entity_index],
                             key=lambda p: (p.brush_index, p.face_index))
        collision_faces = [p for p in actor_polys
                           if p.collision_mode != COLLISION_NONE
                           and not p.texture.startswith("__")
                           and p.texture != "TB_empty"]
        face_order_index = 0
        for poly in collision_faces:
            face_order[(poly.entity_index, poly.brush_index, poly.face_index)] = (
                solid_id, face_order_index,
            )
            face_order_index += 1
        if collision_faces:
            points = [v for poly in collision_faces for v in poly.verts]
            xs = [p[0] for p in points]
            ys = [-p[2] for p in points]
            actor_records.append(SolidGridRecord(
                solid_id=solid_id,
                min_x=min(xs), min_y=min(ys),
                max_x=max(xs), max_y=max(ys),
            ))

    emitted_face_keys = {
        (p.entity_index, p.brush_index, p.face_index)
        for p in all_polys
        if p.collision_mode != COLLISION_NONE
        and not p.texture.startswith("__")
        and p.texture != "TB_empty"
    }
    return face_order, records + actor_records, emitted_face_keys - set(face_order)
