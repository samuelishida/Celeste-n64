#!/usr/bin/env python3
"""Current-map DSOL/MPP5 identity, transform, range, and corruption gates."""

import math
import sys
import tempfile
from collections import Counter
from dataclasses import replace
from pathlib import Path
from types import SimpleNamespace

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tools"))

from artifact_hash import artifact_hash
from dsol_format import DsolFace, decode, encode, read
from mappack_format import (
    MapPackV5, V2Room, V5DynamicSolid, read_mappack_v5_binary,
    write_mappack_v5_binary,
)
from ogworld.collision import build_global_collision
from ogworld.chunking import condition_texture_uvs, partition_world
from ogworld.geometry import build_world_geometry
from ogworld.parse import build_world_ir
from ogworld.parse import _parse_int_property, _resolve_dynamic_target
from ogmap_lib import parse_map
from writers.dynamic_solid_writer import write_dynamic_solids
from writers.colmesh_world_writer import source_face_id_map


def close3(a, b, tolerance=2e-4):
    return all(abs(x - y) <= tolerance for x, y in zip(a, b))


def test_current_map_dynamic_artifacts():
    try:
        encode([], [DsolFace(1, 0, 0, 1, 0, 0, 3)] * 8193, [])
        raise AssertionError("DSOL encoder accepted runtime face-cap overflow")
    except ValueError:
        pass

    source_map = REPO / "assets/og_converted/maps/1.map"
    build = build_world_ir(str(source_map), scale=0.2, strict=True)
    polygons, diagnostics = build_world_geometry(build, scale=0.2)
    chunks, _ = partition_world(build, polygons, 1200, 0.2)
    scene = build_global_collision(polygons, scale=0.2)
    assert len(build.dynamic_solids) == 118
    dynamic_ids = {record.source_id for record in build.dynamic_solids}
    assert all(poly.entity_index not in dynamic_ids
               for chunk in chunks.values() for poly in chunk.polygons)
    assert not [item for item in diagnostics
                if any(item.startswith(f"face {source_id}:")
                       for source_id in dynamic_ids
                       if " produced empty polygon" not in item)]

    expected_classes = Counter({
        "TrafficBlock": 9, "FallingBlock": 68, "FloatyBlock": 22,
        "MovingBlock": 3, "BreakBlock": 15, "DoubleDashPuzzleBlock": 1,
    })
    assert Counter(record.classname for record in build.dynamic_solids) == expected_classes
    parsed_map = parse_map(str(source_map))
    for record in build.dynamic_solids:
        entity = parsed_map.entities[record.source_id]
        expected_flags = 0
        if entity.classname == "MovingBlock" and _parse_int_property(entity.properties, "slow", 0) > 0:
            expected_flags |= 1
        if entity.classname == "FallingBlock" and _parse_int_property(entity.properties, "secret", 0) != 0:
            expected_flags |= 1
        assert record.flags == expected_flags

    # OG lookup is first-match in source order and does not filter by class.
    targets = [
        SimpleNamespace(classname="Decoration", origin=(10, 20, 30),
                        properties={"targetname": "duplicate"}),
        SimpleNamespace(classname="Node", origin=(90, 80, 70),
                        properties={"targetname": "duplicate"}),
    ]
    assert _resolve_dynamic_target(targets, "duplicate", 0.2) == (0, (2.0, 6.0, -4.0))
    assert _resolve_dynamic_target(targets, "missing", 0.2) == (0xFFFFFFFF, (0.0, 0.0, 0.0))
    assert _resolve_dynamic_target(targets, "", 0.2) == (0xFFFFFFFF, (0.0, 0.0, 0.0))

    with tempfile.TemporaryDirectory(prefix="dynamic-solid-artifact-") as tmp:
        root = Path(tmp)
        dsol_path = root / "test.dsol"
        stats = write_dynamic_solids(build, polygons, scene, str(dsol_path))
        actors, faces, vertices = read(dsol_path)
        assert (stats["actors"], stats["faces"], stats["vertices"]) == (
            118, 853, 3519)
        assert len(actors) == len(build.dynamic_solids)

        source_face_ids = source_face_id_map(scene)
        poly_by_key = {
            (p.entity_index, p.brush_index, p.face_index): p
            for p in polygons if p.actor_local_verts
        }
        record_by_id = {r.source_id: r for r in build.dynamic_solids}
        emitted_face_keys = set()
        assert any(len({key[1] for key in poly_by_key if key[0] == source_id}) > 1
                   for source_id in dynamic_ids)
        for actor in actors:
            record = record_by_id[actor.source_id]
            assert actor.solid_id == record.solid_id
            assert actor.first_face == record.dsol_first_face
            assert actor.face_count == record.dsol_face_count
            assert actor.first_vertex == record.dsol_first_vertex
            assert actor.vertex_count == record.dsol_vertex_count
            c, s = math.cos(actor.yaw_radians), math.sin(actor.yaw_radians)
            actor_face_keys = []
            for face in faces[actor.first_face:actor.first_face + actor.face_count]:
                key = (face.source_id, face.brush_index, face.face_index)
                assert key not in emitted_face_keys
                emitted_face_keys.add(key)
                actor_face_keys.append(key)
                poly = poly_by_key[key]
                assert face.cms_source_face_id == source_face_ids.get(key, 0)
                assert face.material_id == poly.material_id
                profile = build.texture_profiles.get(poly.texture)
                expected_uvs = condition_texture_uvs(poly.uvs, profile) if profile else poly.uvs
                face_vertices = vertices[face.first_vertex:face.first_vertex + face.vertex_count]
                assert all(
                    abs(got.uv[axis] - expected[axis]) < 1e-5
                    for got, expected in zip(face_vertices, expected_uvs)
                    for axis in range(2)
                )
                assert all(
                    -1024.0 - 1e-5 <= component <= 1023.96875 + 1e-5
                    for vertex in face_vertices for component in vertex.uv
                )
                transformed = [
                    (actor.position[0] + c * v.position[0] + s * v.position[2],
                     actor.position[1] + v.position[1],
                     actor.position[2] - s * v.position[0] + c * v.position[2])
                    for v in face_vertices
                ]
                assert len(transformed) == len(poly.verts)
                assert all(close3(a, b) for a, b in zip(transformed, poly.verts))
            assert actor_face_keys == sorted(actor_face_keys, key=lambda key: (key[1], key[2]))
        assert len(emitted_face_keys) == stats["faces"]

        hashed = artifact_hash(dsol_path)
        room = V2Room(
            "cell_00_00", "rom:/lvl/test/cell_00_00.lvl", (0, 0, 0),
            (0, 0, 0), (1, 1, 1), 0, 0,
            {"+X": "", "-X": "", "+Z": "", "-Z": ""}, [],
        )
        dynamic_records = [V5DynamicSolid(
            source_id=r.source_id, solid_id=r.solid_id, kind=r.kind,
            flags=r.flags, position=r.position,
            yaw_radians=math.radians(r.yaw_degrees),
            cms_first_source_face_id=r.cms_first_source_face_id,
            cms_face_count=r.cms_face_count,
            dsol_first_face=r.dsol_first_face,
            dsol_face_count=r.dsol_face_count,
            dsol_first_vertex=r.dsol_first_vertex,
            dsol_vertex_count=r.dsol_vertex_count,
            target_source_id=r.target_source_id,
            target_position=r.target_position,
        ) for r in build.dynamic_solids]
        pack = MapPackV5(
            "test", build.source_sha256, 2, 0.2, 1200,
            "rom:/lvl/test/test.colmesh", 0, 0, 0, 0, 0,
            "cell_00_00", [room], [], "rom:/lvl/test/test.dsol",
            hashed["crc32"], hashed["size_bytes"], dynamic_records,
        )
        pack_path = root / "test.mappack"
        write_mappack_v5_binary(pack, str(pack_path))
        loaded = read_mappack_v5_binary(str(pack_path))
        assert len(loaded.dynamic_solids) == 118
        assert loaded.dsol_crc32 == hashed["crc32"]
        assert loaded.dynamic_solids[0].source_id == dynamic_records[0].source_id
        assert loaded.dynamic_solids[-1].dsol_vertex_count == dynamic_records[-1].dsol_vertex_count

        overflow_pack = replace(pack, dynamic_solids=dynamic_records * 3)
        try:
            write_mappack_v5_binary(overflow_pack, str(pack_path))
            raise AssertionError("MPP5 writer accepted dynamic-solid overflow")
        except ValueError:
            pass

        bad_path_pack = replace(pack, dsol_path="rom:/lvl/../outside.dsol")
        try:
            write_mappack_v5_binary(bad_path_pack, str(pack_path))
            raise AssertionError("MPP5 writer accepted unsafe DSOL path")
        except ValueError:
            pass

        original_pack = pack_path.read_bytes()
        pack_path.write_bytes(original_pack + b"x")
        try:
            read_mappack_v5_binary(str(pack_path))
            raise AssertionError("MPP5 reader accepted trailing bytes")
        except ValueError:
            pass

        try:
            decode(dsol_path.read_bytes() + b"x")
            raise AssertionError("DSOL decoder accepted trailing bytes")
        except ValueError:
            pass


if __name__ == "__main__":
    test_current_map_dynamic_artifacts()
    print("PASS: dynamic_solid_artifact_smoke")
