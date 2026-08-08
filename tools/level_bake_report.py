#!/usr/bin/env python3
"""Emit deterministic geometry diagnostics for a baked level source/output pair."""

from __future__ import annotations

import argparse
import hashlib
import math
import sys
from collections import Counter
from pathlib import Path
from typing import Iterable

# Add tools to path for imports
sys.path.insert(0, str(Path(__file__).parent))

from ogmap_lib import parse_map, Entity as OgEntity
from lvl_format import LvlFile
from colmesh_bake import material_flags


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


def _read_materials(lvl_path: Path, lvl) -> list[str]:
    """Read material list from manifest sidecar; fall back to strings[3:] for legacy files."""
    manifest = lvl_path.with_suffix(".manifest")
    if manifest.exists():
        return [line.strip() for line in manifest.read_text().splitlines() if line.strip()]
    reserved = {lvl.skybox_str_id, lvl.music_str_id, lvl.ambience_str_id}
    return [s for i, s in enumerate(lvl.strings) if i not in reserved]


def summarize(map_path: Path, lvl_path: Path) -> list[str]:
    parsed_map = parse_map(str(map_path))
    lvl = LvlFile.read(str(lvl_path))
    with lvl_path.open("rb") as f:
        magic = f.read(4).decode("ascii")
        version = int.from_bytes(f.read(4), "big")

    brushes_by_class = Counter()
    source_faces_by_class = Counter()
    for entity in parsed_map.entities:
        classname = entity.classname
        brushes = entity.brushes
        brushes_by_class[classname] += len(brushes)
        source_faces_by_class[classname] += sum(len(brush.faces) for brush in brushes)

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
        f"lvl_header=magic:{magic} version:{version}",
        f"map_sha256={hashlib.sha256(map_path.read_bytes()).hexdigest()}",
        f"lvl_sha256={hashlib.sha256(lvl_path.read_bytes()).hexdigest()}",
        f"brushes_by_class={dict(sorted(brushes_by_class.items()))}",
        f"source_faces_by_class={dict(sorted(source_faces_by_class.items()))}",
        f"baked_counts=colliders:{len(lvl.colliders)} faces:{len(lvl.faces)} vertices:{len(lvl.vertices)} entities:{len(lvl.entities)}",
        f"duplicate_vertex_faces={duplicate_vertex_faces}",
        f"first_fan_degenerate_faces={first_fan_degenerate_faces}",
        f"reversed_winding_faces={reversed_winding_faces}",
        f"materials={_read_materials(lvl_path, lvl)}",
    ]

    # Material flag summary — accumulate colmesh material flags per LVL face.
    # Only solid faces (face.flags & 0x01) are processed by colmesh_bake.py;
    # visual-only faces (face.flags & 0x02) are skipped.
    materials = _read_materials(lvl_path, lvl)
    mat_flag_counts: Counter[str] = Counter()
    for face in lvl.faces:
        is_visual = bool(face.flags & 0x02)
        if is_visual:
            mat_flag_counts["visual"] += 1
            continue
        mat_name = materials[face.material_id] if face.material_id < len(materials) else "unknown"
        flags = material_flags(mat_name)
        if flags == 0:
            mat_flag_counts["trigger"] += 1
        else:
            if flags & 0x0001:
                mat_flag_counts["solid"] += 1
            if flags & 0x0002:
                mat_flag_counts["oneway"] += 1
            if flags & 0x0004:
                mat_flag_counts["death"] += 1
            if flags & 0x0008:
                mat_flag_counts["climbable"] += 1
            if flags & 0x0010:
                mat_flag_counts["ice"] += 1

    lines.append(
        f"material_flags=solid:{mat_flag_counts.get('solid', 0)} "
        f"death:{mat_flag_counts.get('death', 0)} "
        f"climbable:{mat_flag_counts.get('climbable', 0)} "
        f"ice:{mat_flag_counts.get('ice', 0)} "
        f"oneway:{mat_flag_counts.get('oneway', 0)} "
        f"visual:{mat_flag_counts.get('visual', 0)}"
    )

    return lines


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("map_path", type=Path)
    parser.add_argument("lvl_path", type=Path)
    args = parser.parse_args()
    print("\n".join(summarize(args.map_path, args.lvl_path)))


if __name__ == "__main__":
    main()
