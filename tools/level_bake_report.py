#!/usr/bin/env python3
"""Emit deterministic geometry diagnostics for a baked level source/output pair."""

from __future__ import annotations

import argparse
import hashlib
import math
from collections import Counter
from pathlib import Path
from typing import Iterable

from bake_map import parse_map_file
from lvl_format import LvlFile


def _cross(a, b, c):
    ux, uy, uz = b[0] - a[0], b[1] - a[1], b[2] - a[2]
    vx, vy, vz = c[0] - a[0], c[1] - a[1], c[2] - a[2]
    return (
        uy * vz - uz * vy,
        uz * vx - ux * vz,
        ux * vy - uy * vx,
    )


def _mag(v) -> float:
    return math.sqrt(sum(x * x for x in v))


def _newell(points: list[tuple[float, float, float]]):
    nx = ny = nz = 0.0
    for a, b in zip(points, points[1:] + points[:1]):
        nx += (a[1] - b[1]) * (a[2] + b[2])
        ny += (a[2] - b[2]) * (a[0] + b[0])
        nz += (a[0] - b[0]) * (a[1] + b[1])
    return nx, ny, nz


def summarize(map_path: Path, lvl_path: Path) -> list[str]:
    entities = parse_map_file(str(map_path))
    lvl = LvlFile.read(str(lvl_path))

    brushes_by_class = Counter()
    source_faces_by_class = Counter()
    for entity in entities:
        classname = entity.get("classname", "")
        brushes = entity.get("brushes", [])
        brushes_by_class[classname] += len(brushes)
        source_faces_by_class[classname] += sum(len(brush) for brush in brushes)

    duplicate_vertex_faces = 0
    first_fan_degenerate_faces = 0
    reversed_winding_faces = 0
    for face in lvl.faces:
        points = [lvl.vertices[i].pos for i in range(face.vertex_start, face.vertex_start + face.vertex_count)]
        if len({tuple(round(c, 6) for c in p) for p in points}) < len(points):
            duplicate_vertex_faces += 1
        if len(points) >= 3 and _mag(_cross(points[0], points[1], points[2])) < 1e-6:
            first_fan_degenerate_faces += 1
        poly_normal = _newell(points)
        if sum(poly_normal[i] * face.normal[i] for i in range(3)) < 0:
            reversed_winding_faces += 1

    lines = [
        f"map_sha256={hashlib.sha256(map_path.read_bytes()).hexdigest()}",
        f"lvl_sha256={hashlib.sha256(lvl_path.read_bytes()).hexdigest()}",
        f"brushes_by_class={dict(sorted(brushes_by_class.items()))}",
        f"source_faces_by_class={dict(sorted(source_faces_by_class.items()))}",
        f"baked_counts=colliders:{len(lvl.colliders)} faces:{len(lvl.faces)} vertices:{len(lvl.vertices)} entities:{len(lvl.entities)}",
        f"duplicate_vertex_faces={duplicate_vertex_faces}",
        f"first_fan_degenerate_faces={first_fan_degenerate_faces}",
        f"reversed_winding_faces={reversed_winding_faces}",
        f"materials={lvl.strings[3:]}",
    ]
    return lines


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("map_path", type=Path)
    parser.add_argument("lvl_path", type=Path)
    args = parser.parse_args()
    print("\n".join(summarize(args.map_path, args.lvl_path)))


if __name__ == "__main__":
    main()
