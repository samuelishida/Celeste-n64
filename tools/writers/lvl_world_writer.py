#!/usr/bin/env python3
"""LVL world writer (Inc 4).

Writes one LVL2 visual/entity room per visual cell from the canonical
polygons. Preserves entity properties in the props blob where the runtime
needs them, but does NOT use LVL entity order for Start/anchor routing (that
is manifest data).
"""

import sys
import struct
from pathlib import Path
from typing import List, Tuple, Optional

# Add tools to path for imports.
sys.path.insert(0, str(Path(__file__).parent.parent))

from ogworld.model import ChunkInput, WorldPolygon, SpawnRecord
from lvl_format import LvlFile, Face as LvlFace, Vertex as LvlVertex, Entity as LvlEntity
from entity_ids import id_of

# Runtime caps (must match src/user/gameplay/world/level_loader.hpp).
K_MAX_FACES = 1024
K_MAX_VERTICES = 8192


def write_lvl_room(
    chunk: ChunkInput,
    texture_manifest: List[str],
    out_path: str,
    scale: float = 0.2,
) -> dict:
    """Write one LVL2 room for a visual cell.

    Returns a stats dict. Fails (raises ValueError) if the cell exceeds the
    runtime face/vertex caps.
    """
    lvl = LvlFile()
    lvl.strings = list(texture_manifest)
    string_to_id = {s: i for i, s in enumerate(texture_manifest)}

    # Visual polygons -> faces + vertices.
    vert_start = 0
    for poly in chunk.polygons:
        # Face flags: solid if collision_mode != none, else visual_only.
        if poly.collision_mode != "none":
            flags = 0x01  # solid
        else:
            flags = 0x02  # visual_only
        mat_id = string_to_id.get(poly.texture, 0)
        for i, v in enumerate(poly.verts):
            lvl.vertices.append(LvlVertex(pos=v, uv=poly.uvs[i]))
        lvl.faces.append(LvlFace(
            vertex_start=vert_start,
            vertex_count=len(poly.verts),
            material_id=mat_id,
            normal=poly.normal,
            flags=flags,
        ))
        vert_start += len(poly.verts)

    # Entity spawns from the cell's point-entity spawn records.
    for spawn in chunk.spawns:
        ent_id = id_of(spawn.classname)
        if ent_id is None:
            continue
        lvl.entities.append(LvlEntity(
            classname_id=ent_id,
            position=spawn.position,
        ))

    # Cap checks.
    if len(lvl.faces) > K_MAX_FACES:
        raise ValueError(
            f"cell faces {len(lvl.faces)} > cap {K_MAX_FACES}")
    if len(lvl.vertices) > K_MAX_VERTICES:
        raise ValueError(
            f"cell verts {len(lvl.vertices)} > cap {K_MAX_VERTICES}")

    lvl.write(out_path)
    return {
        "faces": len(lvl.faces),
        "vertices": len(lvl.vertices),
        "entities": len(lvl.entities),
    }
