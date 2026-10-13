#!/usr/bin/env python3
"""Check initial OG Solid actor transform and SolidGrid order parity."""

from pathlib import Path
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

from ogworld.parse import build_world_ir
from ogworld.geometry import build_world_geometry
from ogworld.collision import build_global_collision, emits_collision
from ogworld.class_policy import DYNAMIC_STATIC_PROXY, policy_for


def box(mins, maxs):
    x0, y0, z0 = mins
    x1, y1, z1 = maxs
    faces = (
        ((x1, y0, z0), (x1, y0, z1), (x1, y1, z0)),
        ((x0, y0, z0), (x0, y1, z0), (x0, y0, z1)),
        ((x0, y1, z0), (x1, y1, z0), (x0, y1, z1)),
        ((x0, y0, z0), (x0, y0, z1), (x1, y0, z0)),
        ((x0, y0, z1), (x0, y1, z1), (x1, y0, z1)),
        ((x0, y0, z0), (x1, y0, z0), (x0, y1, z0)),
    )
    lines = ["{"]
    for points in faces:
        coords = " ".join("( %s %s %s )" % p for p in points)
        lines.append(f"{coords} rock_1 0 0 0 1 1")
    lines.append("}")
    return "\n".join(lines)


def entity(classname, brushes=(), properties=()):
    lines = ["{", f'"classname" "{classname}"']
    lines.extend(f'"{key}" "{value}"' for key, value in properties)
    lines.extend(brushes)
    lines.append("}")
    return "\n".join(lines)


def close_vec(actual, expected, eps=1e-4):
    return all(abs(a - e) <= eps for a, e in zip(actual, expected))


def main() -> int:
    # One worldspawn chunk with a func_group child brush, then transformed and
    # untransformed actor Solids to check OG collection and transform behavior.
    source = "\n".join((
        entity("worldspawn", (box((0, -1000, 0), (1000, 0, 64)),)),
        entity("func_group", (box((100, -800, 0), (120, -780, 40)),)),
        entity("TrafficBlock", (
            box((0, 0, 0), (100, 50, 40)),
            box((200, 0, 0), (220, 50, 40)),
        ), (("origin", "500 -500 64"), ("angle", "90"))),
        entity("FallingBlock", (box((300, 0, 0), (340, 50, 40)),)),
        entity("FloatyBlock", (box((400, 0, 0), (440, 50, 40)),),
               (("angle", "90"),)),
    ))
    with tempfile.TemporaryDirectory(prefix="og-entity-solid-") as temp_dir:
        map_path = Path(temp_dir) / "entity-solid.map"
        map_path.write_text(source, encoding="utf-8")
        build = build_world_ir(str(map_path), scale=0.2, strict=True)
        polygons, diagnostics = build_world_geometry(build, scale=0.2)
        if diagnostics:
            raise AssertionError(f"unexpected geometry diagnostics: {diagnostics}")

    actor_polys = [p for p in polygons if p.classname == "TrafficBlock"]
    if len(actor_polys) != 12:
        raise AssertionError(f"expected 12 actor faces, got {len(actor_polys)}")

    # OG centers combined brushes at (22, 4, -5) in port coordinates, rotates
    # local geometry +90 degrees around source Z, then translates to origin.
    actor_verts = [v for p in actor_polys for v in p.verts]
    mins = tuple(min(v[i] for v in actor_verts) for i in range(3))
    maxs = tuple(max(v[i] for v in actor_verts) for i in range(3))
    if not close_vec(mins, (95.0, 8.8, 78.0)):
        raise AssertionError(f"transformed actor mins {mins}, expected (95, 8.8, 78)")
    if not close_vec(maxs, (105.0, 16.8, 122.0)):
        raise AssertionError(f"transformed actor maxs {maxs}, expected (105, 16.8, 122)")
    if not any(close_vec(v, (105.0, 8.8, 122.0)) for v in actor_verts):
        raise AssertionError("explicit origin/yaw did not transform expected source vertex")

    source_x_face = next(p for p in actor_polys if p.brush_index == 0
                         and p.face_index == 0)
    if not close_vec(source_x_face.normal, (0.0, 0.0, -1.0)):
        raise AssertionError(f"actor face normal did not rotate with solid: {source_x_face.normal}")

    default_polys = [p for p in polygons if p.classname == "FallingBlock"]
    default_verts = [v for p in default_polys for v in p.verts]
    default_mins = tuple(min(v[i] for v in default_verts) for i in range(3))
    default_maxs = tuple(max(v[i] for v in default_verts) for i in range(3))
    if not close_vec(default_mins, (60.0, 0.0, -10.0)) or not close_vec(
            default_maxs, (68.0, 8.0, 0.0)):
        raise AssertionError(f"default actor transform moved geometry: {default_mins}..{default_maxs}")

    yaw_only_polys = [p for p in polygons if p.classname == "FloatyBlock"]
    yaw_only_verts = [v for p in yaw_only_polys for v in p.verts]
    yaw_only_mins = tuple(min(v[i] for v in yaw_only_verts) for i in range(3))
    yaw_only_maxs = tuple(max(v[i] for v in yaw_only_verts) for i in range(3))
    if not close_vec(yaw_only_mins, (79.0, 0.0, -9.0)) or not close_vec(
            yaw_only_maxs, (89.0, 8.0, -1.0)):
        raise AssertionError(f"yaw without origin mismatch: {yaw_only_mins}..{yaw_only_maxs}")

    scene = build_global_collision(polygons, scale=0.2)
    world_ids = {t.og_solid_id for t in scene.tris if t.entity_index == 0}
    func_group_tris = [t for t in scene.tris if t.entity_index == 1]
    if not func_group_tris or {t.og_solid_id for t in func_group_tris} != {0}:
        raise AssertionError("func_group child brush did not join first static Solid")
    if sorted({t.og_face_order for t in func_group_tris}) != list(range(6)):
        raise AssertionError("func_group child brush lost static Solid face ordering")
    actor_tris = [t for t in scene.tris if t.entity_index == 2 and emits_collision(
            next(p for p in actor_polys if (p.brush_index, p.face_index) ==
             (t.brush_index, t.face_index)))]
    if world_ids != {0}:
        raise AssertionError(f"worldspawn should occupy first SolidGrid ID, got {world_ids}")
    if not actor_tris or {t.og_solid_id for t in actor_tris} != {1}:
        raise AssertionError("entity-owned solid did not follow worldspawn insertion order")
    orders = sorted({t.og_face_order for t in actor_tris})
    if orders != list(range(12)):
        raise AssertionError(f"entity faces not source-ordered in one Solid: {orders}")
    default_tris = [t for t in scene.tris if t.entity_index == 3]
    if not default_tris or {t.og_solid_id for t in default_tris} != {2}:
        raise AssertionError("default actor Solid did not follow map entity insertion order")
    yaw_only_tris = [t for t in scene.tris if t.entity_index == 4]
    if not yaw_only_tris or {t.og_solid_id for t in yaw_only_tris} != {3}:
        raise AssertionError("yaw-only actor Solid did not follow map entity insertion order")

    print("OG entity Solid transform + insertion order: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
