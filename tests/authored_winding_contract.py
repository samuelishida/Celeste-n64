#!/usr/bin/env python3
"""Prove emitted normals face away from source brushes and baked CMSH agrees."""
from pathlib import Path
import math
import os
import struct
import sys

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from ogworld.parse import build_world_ir
from ogworld.geometry import build_world_geometry
from ogworld.collision import build_global_collision
from ogmap_lib.brush_geom import fan_triangulate, outward_face_normal
from writers.colmesh_writer import quantize, compute_quant

assert outward_face_normal({"normal": (0, -1, 0)}) == (0, 1, 0)
world = build_world_ir(ROOT / "assets/og_converted/maps/1.map", scale=.2, eps=1e-4)
polygons, _ = build_world_geometry(world, scale=.2, eps=1e-4)
groups = {}
for polygon in polygons:
    groups.setdefault((polygon.entity_index, polygon.brush_index), []).append(polygon)
centers = {}
away = 0
for key, faces in groups.items():
    # A mean of actual convex-brush vertices lies inside that brush. Checking
    # against its center is independent of the emission normal/winding match.
    vertices = list({tuple(v) for face in faces for v in face.verts})
    center = tuple(sum(v[x] for v in vertices) / len(vertices) for x in range(3))
    centers[key] = center
    for face in faces:
        signed = sum(face.normal[x] * (center[x] - face.verts[0][x]) for x in range(3))
        assert signed <= 1e-4, ("emitted normal faces brush interior", key, face.face_index, signed)
        away += signed < -1e-4
assert away > 7000

scene = build_global_collision(polygons, scale=.2, eps=1e-4)
origin, scale = compute_quant(scene.verts)
source = {}
for face in polygons:
    for tri in fan_triangulate(face.verts):
        key = tuple(sorted(quantize(face.verts[x], origin, scale) for x in tri))
        source.setdefault(key, []).append(face)
artifact_path = Path(os.environ.get(
    "OG_COLLISION_PATH",
    ROOT / "build/bake-fc-1200/staging/forsyken-city.colmesh",
))
raw = artifact_path.read_bytes()
header = struct.unpack(">4sHH6h4f8I", raw[:68])
assert header[0] == b"CMSH"
vertex_count, triangle_count = header[13:15]
vertex_offset, triangle_offset = header[17:19]
vertices = [struct.unpack_from(">3h", raw, vertex_offset + i * 6) for i in range(vertex_count)]
assert triangle_count == len(scene.tris)
known = {}
for index in range(triangle_count):
    i, j, k, _, _, _ = struct.unpack_from(">6H", raw, triangle_offset + index * 12)
    a, b, c = vertices[i], vertices[j], vertices[k]
    faces = source[tuple(sorted((a, b, c)))]
    u, v = tuple(b[x] - a[x] for x in range(3)), tuple(c[x] - a[x] for x in range(3))
    cross = (u[1]*v[2]-u[2]*v[1], u[2]*v[0]-u[0]*v[2], u[0]*v[1]-u[1]*v[0])
    length = math.sqrt(sum(x*x for x in cross))
    assert length > 0, ("quantized degenerate triangle", index)
    normal = tuple(x / length for x in cross)
    world_a = tuple(origin[x] + a[x] * scale for x in range(3))
    assert any(sum(normal[x] * face.normal[x] for x in range(3)) > 0 for face in faces)
    assert any(sum(normal[x] * (centers[(face.entity_index, face.brush_index)][x] - world_a[x])
                   for x in range(3)) <= max(.001, scale * 4) for face in faces), index
    for face in faces:
        identity = (face.entity_index, face.brush_index, face.face_index)
        if identity in [(0,0,4), (0,0,3), (0,607,1), (0,607,3)]:
            known[identity] = normal
# Start platform's top and bottom: raw source inward planes must become
# outward emitted surfaces, so a downward gameplay ray selects the top.
assert known[(0,0,4)][1] > .99
assert known[(0,0,3)][1] < -.99
assert known[(0,607,1)][2] > .99
print(f"PASS: authored winding {away} outward source faces, {triangle_count} outward baked triangles; Start top +Y, pillar front +Z")
