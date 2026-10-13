#!/usr/bin/env python3
"""Real-map contract for OG static-world SolidGrid metadata."""

from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

from ogworld.parse import build_world_ir
from ogworld.geometry import build_world_geometry
from ogworld.collision import build_global_collision, collision_budget, emits_collision
from ogworld.class_policy import DYNAMIC_STATIC_PROXY, policy_for


def main() -> int:
    source_map = ROOT / "assets/og_converted/maps/1.map"
    build = build_world_ir(str(source_map), scale=0.2, strict=False)
    polygons, _ = build_world_geometry(build, scale=0.2)
    scene = build_global_collision(polygons, scale=0.2)

    static_entity_ids = {
        brush.entity_index for brush in build.source_brushes
        if brush.classname in ("worldspawn", "func_group")
    }
    static_faces = {
        (p.entity_index, p.brush_index, p.face_index)
        for p in polygons
        if p.classname in ("worldspawn", "func_group") and emits_collision(p)
    }
    ordered_faces = {
        (t.entity_index, t.brush_index, t.face_index)
        for t in scene.tris if t.entity_index in static_entity_ids and t.og_solid_id >= 0
    }
    if static_faces != ordered_faces:
        missing = sorted(static_faces - ordered_faces)[:5]
        extra = sorted(ordered_faces - static_faces)[:5]
        raise AssertionError(f"static face metadata mismatch missing={missing} extra={extra}")
    if not scene.og_solid_grid:
        raise AssertionError("real map produced no OG SolidGrid records")
    ids = [record.solid_id for record in scene.og_solid_grid]
    if ids != sorted(set(ids)):
        raise AssertionError("SolidGrid records are not in unique insertion order")
    if any(record.min_x > record.max_x or record.min_y > record.max_y
           for record in scene.og_solid_grid):
        raise AssertionError("SolidGrid record has inverted bounds")
    if any(t.og_solid_id < 0 or t.og_face_order < 0
           for t in scene.tris if t.entity_index == 0):
        raise AssertionError("worldspawn collision triangle lacks OG face identity")
    actor_faces = {
        (p.entity_index, p.brush_index, p.face_index)
        for p in polygons if emits_collision(p)
        and policy_for(p.classname) is not None
        and policy_for(p.classname).dynamic == DYNAMIC_STATIC_PROXY
    }
    actor_entity_ids = {
        brush.entity_index for brush in build.source_brushes
        if policy_for(brush.classname) is not None
        and policy_for(brush.classname).dynamic == DYNAMIC_STATIC_PROXY
    }
    ordered_actor_faces = {
        (t.entity_index, t.brush_index, t.face_index)
        for t in scene.tris if t.entity_index in actor_entity_ids and t.og_solid_id >= 0
    }
    if actor_faces != ordered_actor_faces:
        raise AssertionError("entity-owned Solid face metadata is incomplete")
    per_solid = {}
    for triangle in scene.tris:
        if triangle.og_solid_id >= 0:
            key = (triangle.entity_index, triangle.brush_index, triangle.face_index)
            per_solid.setdefault(triangle.og_solid_id, {})[key] = triangle.og_face_order
    if any(sorted(set(face_orders.values())) != list(range(len(set(face_orders.values()))))
           for face_orders in per_solid.values()):
        raise AssertionError("solid face order is not contiguous")
    budget = collision_budget(scene)
    if budget["og_solid_grid_records"] != len(scene.og_solid_grid):
        raise AssertionError("collision budget omits CMSH v2 records")

    print(f"OG wall order: {len(static_faces)} static faces + "
          f"{len(actor_faces)} actor faces, {len(scene.og_solid_grid)} solids, "
          f"{len(scene.tris)} collision triangles: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
