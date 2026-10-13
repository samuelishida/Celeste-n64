#!/usr/bin/env python3
"""Write actor-local dynamic-solid geometry and enrich MPP5 records."""

from __future__ import annotations

import math
from dataclasses import replace
from pathlib import Path

from dsol_format import DsolActor, DsolFace, DsolVertex, encode
from ogworld.chunking import condition_texture_uvs
from writers.colmesh_world_writer import source_face_id_map


def write_dynamic_solids(build, polygons, collision_scene, out_path: str) -> dict:
    """Emit one contiguous actor-local mesh per dynamic source entity.

    CMSH source-face IDs are obtained from the same canonical helper used by
    the CMSH writer. Collision ranges therefore identify exact authored faces,
    not BVH triangle order.
    """
    records = sorted(build.dynamic_solids, key=lambda record: record.source_id)
    polygons_by_actor = {}
    for poly in polygons:
        if poly.actor_local_verts:
            polygons_by_actor.setdefault(poly.entity_index, []).append(poly)

    face_ids = source_face_id_map(collision_scene)
    solid_ids_by_actor = {}
    face_ids_by_actor = {}
    for tri in collision_scene.tris:
        if tri.og_solid_id < 0:
            continue
        solid_ids_by_actor.setdefault(tri.entity_index, set()).add(tri.og_solid_id)
        key = (tri.entity_index, tri.brush_index, tri.face_index)
        face_ids_by_actor.setdefault(tri.entity_index, set()).add(face_ids[key])

    actors = []
    faces = []
    vertices = []
    enriched = []
    for record in records:
        actor_polys = sorted(polygons_by_actor.get(record.source_id, ()),
                             key=lambda poly: (poly.brush_index, poly.face_index))
        if not actor_polys:
            raise ValueError(f"dynamic solid {record.source_id} has no render faces")
        solid_ids = solid_ids_by_actor.get(record.source_id, set())
        if len(solid_ids) != 1:
            raise ValueError(
                f"dynamic solid {record.source_id} must map to one CMSH OG solid id; "
                f"got {sorted(solid_ids)}")
        solid_id = next(iter(solid_ids))
        actor_face_ids = sorted(face_ids_by_actor[record.source_id])
        if not actor_face_ids or actor_face_ids != list(
                range(actor_face_ids[0], actor_face_ids[-1] + 1)):
            raise ValueError(f"dynamic solid {record.source_id} CMSH face ids are not contiguous")

        first_face = len(faces)
        first_vertex = len(vertices)
        for poly in actor_polys:
            if len(poly.actor_local_verts) != len(poly.uvs):
                raise ValueError(
                    f"dynamic face {record.source_id}:{poly.brush_index}:{poly.face_index} "
                    "has mismatched local vertex/UV counts")
            if poly.actor_local_normal is None:
                raise ValueError(f"dynamic face {record.source_id} lacks actor-local normal")
            profile = build.texture_profiles.get(poly.texture)
            uvs = condition_texture_uvs(poly.uvs, profile) if profile else poly.uvs
            face_first_vertex = len(vertices)
            cms_face_id = face_ids.get(
                (poly.entity_index, poly.brush_index, poly.face_index), 0)
            for position, uv in zip(poly.actor_local_verts, uvs):
                vertices.append(DsolVertex(position, poly.actor_local_normal, uv))
            faces.append(DsolFace(
                source_id=record.source_id,
                brush_index=poly.brush_index,
                face_index=poly.face_index,
                cms_source_face_id=cms_face_id,
                material_id=poly.material_id,
                first_vertex=face_first_vertex,
                vertex_count=len(poly.actor_local_verts),
                flags=0,
            ))

        first_cms_face = actor_face_ids[0]
        cms_face_count = len(actor_face_ids)
        yaw = math.radians(record.yaw_degrees)
        actors.append(DsolActor(
            source_id=record.source_id,
            solid_id=solid_id,
            kind=record.kind,
            flags=record.flags,
            position=record.position,
            yaw_radians=yaw,
            first_face=first_face,
            face_count=len(faces) - first_face,
            first_vertex=first_vertex,
            vertex_count=len(vertices) - first_vertex,
        ))
        enriched.append(replace(
            record,
            solid_id=solid_id,
            cms_first_source_face_id=first_cms_face,
            cms_face_count=cms_face_count,
            dsol_first_face=first_face,
            dsol_face_count=len(faces) - first_face,
            dsol_first_vertex=first_vertex,
            dsol_vertex_count=len(vertices) - first_vertex,
        ))

    data = encode(actors, faces, vertices)
    Path(out_path).write_bytes(data)
    build.dynamic_solids = enriched
    return {
        "actors": len(actors),
        "faces": len(faces),
        "vertices": len(vertices),
        "bytes": len(data),
    }
