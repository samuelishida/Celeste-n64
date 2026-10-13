#!/usr/bin/env python3
"""Authored polygon identity survives global CMSH BVH reordering."""

import struct
import json
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tools"))

from ogworld.collision import CollisionScene
from ogworld.model import CollisionTriangle, MAT_SOLID
from writers.colmesh_world_writer import write_colmesh


def main():
    # Two authored faces, each represented by a two-triangle fan. Their
    # source order differs from their spatial/BVH order.
    scene = CollisionScene()
    scene.verts = [
        (0, 0, 0), (0, 10, 0), (0, 10, 3), (0, 0, 3),
        (0, 0, 3), (0, 10, 3), (0, 10, 10), (0, 0, 10),
    ]
    scene.tris = [
        CollisionTriangle(0, 1, 2, MAT_SOLID, 0, 0, 0),
        CollisionTriangle(0, 2, 3, MAT_SOLID, 0, 0, 0),
        CollisionTriangle(4, 5, 6, MAT_SOLID, 0, 0, 1),
        CollisionTriangle(4, 6, 7, MAT_SOLID, 0, 0, 1),
    ]
    with tempfile.TemporaryDirectory(prefix="colmesh-source-face-") as temp:
        path = Path(temp) / "fixture.colmesh"
        audit_path = Path(temp) / "triangle-source.json"
        stats = write_colmesh(scene, str(path), str(audit_path))
        data = path.read_bytes()
        source_rows = json.loads(audit_path.read_text())["triangle_source"]
        triangle_count = struct.unpack_from(">I", data, 40)[0]
        triangle_offset = struct.unpack_from(">I", data, 56)[0]
        groups = {}
        for index in range(triangle_count):
            row = struct.unpack_from(">HHHHHH", data,
                                     triangle_offset + index * 12)
            face_id, source_face_id = row[4], row[5]
            assert face_id == index, (index, face_id)
            assert source_face_id != 0, (index, source_face_id)
            source_key = source_rows[index]
            groups.setdefault(source_key, set()).add(source_face_id)

        assert stats["triangles"] == 4
        assert groups == {"0:0:0": {1}, "0:0:1": {2}}, groups
    print("PASS: CMSH preserves distinct source-face groups through BVH reorder")


if __name__ == "__main__":
    main()
