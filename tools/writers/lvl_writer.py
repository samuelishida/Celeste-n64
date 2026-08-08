#!/usr/bin/env python3
"""LVL writer — generates LVL2 from ParsedMap.

Reads ParsedMap from ogmap_lib, emits LVL2 binary with per-vertex UVs,
entity spawns, and atmosphere properties from worldspawn.

This is extracted from bake_lvl.py to be a shared writer module.
"""

import sys
import argparse
from pathlib import Path
from typing import List, Dict

# Import library
sys.path.insert(0, str(Path(__file__).parent.parent))
from ogmap_lib import (
    ParsedMap, classify_entity, is_skipped,
    MaterialClass, EntityClass, RenderMode, FaceFilter,
    compute_face_polygon, sort_vertices_ccw, dedupe_polygon_vertices,
    compute_uv, transform_point, transform_normal, is_upward_face,
    extract_atmosphere, build_material_manifest,
    Brush, FaceDef, Vec3,
)
from lvl_format import LvlFile, Face as LvlFace, Vertex as LvlVertex, Entity as LvlEntity
from entity_ids import id_of


class LvlStats:
    """Statistics from LVL generation."""
    def __init__(self):
        self.faces: int = 0
        self.vertices: int = 0
        self.entities: int = 0
        self.colliders: int = 0


def build_lvl_faces(parsed_map: ParsedMap, manifest: List[str], scale: float, lvl: LvlFile) -> None:
    """Build LVL face and vertex data from parsed map.

    UVs are computed on Quake-space points BEFORE game-space transform,
    because compute_uv projects against Quake texture axes (Z-up).
    """
    string_to_id = {s: i for i, s in enumerate(manifest)}

    vert_start = 0
    skipped_brush_classes = set()

    for ent in parsed_map.entities:
        cd = classify_entity(ent)
        if cd is None:
            if ent.brushes:
                skipped_brush_classes.add(ent.classname)
            continue

        # Skip point entities and non-mesh classes
        if cd.render_mode != RenderMode.STATIC_MESH:
            continue
        if not ent.brushes:
            continue

        for brush in ent.brushes:
            faces = brush.faces
            for face_idx, face in enumerate(faces):
                # Upward-only filter for DEATH surfaces
                if cd.face_filter == FaceFilter.UPWARD_ONLY and not is_upward_face(face):
                    continue

                polygon = compute_face_polygon(faces, face_idx)
                if len(polygon) < 3:
                    continue
                polygon = sort_vertices_ccw(polygon, face["normal"])
                polygon = dedupe_polygon_vertices(polygon)
                if len(polygon) < 3:
                    continue

                # Compute UVs on Quake-space points BEFORE transform
                uvs = [compute_uv(v, face, scale) for v in polygon]

                # Transform vertices to game space
                game_verts = [transform_point(v, scale) for v in polygon]

                # Face flags
                if cd.material_class == MaterialClass.VISUAL_ONLY:
                    flags = 0x02  # visual_only
                else:
                    flags = 0x01  # solid

                tex = face["texture"]
                mat_id = string_to_id.get(tex, 0)

                # Write vertices with UVs
                for i, v in enumerate(game_verts):
                    lvl.vertices.append(LvlVertex(pos=v, uv=uvs[i]))

                # Write face
                lvl.faces.append(LvlFace(
                    vertex_start=vert_start,
                    vertex_count=len(game_verts),
                    material_id=mat_id,
                    normal=transform_normal(face["normal"]),
                    flags=flags,
                ))
                vert_start += len(game_verts)

    if skipped_brush_classes:
        print(f"[lvl] skipped brushes for: {', '.join(sorted(skipped_brush_classes))}")


def build_entity_spawns(parsed_map: ParsedMap, scale: float, lvl: LvlFile) -> int:
    """Add entity spawns to LVL from point entities with known EntityClass.

    Skips TrafficBlock (EntityClass 100) since it exceeds entity_ids.py range
    and is handled by bake_nav.py instead.
    """
    count = 0
    for ent in parsed_map.entities:
        cd = classify_entity(ent)
        if cd is None:
            continue
        ec = cd.entity_class
        if ec == EntityClass.NONE:
            continue
        # Skip future-only IDs that exceed runtime's entity_ids range
        ent_id = id_of(ent.classname)
        if ent_id is None:
            continue

        game_pos = transform_point(ent.origin, scale)
        lvl.entities.append(LvlEntity(
            classname_id=ent_id,
            position=game_pos,
        ))
        count += 1
    return count


def apply_atmosphere(lvl: LvlFile, parsed_map: ParsedMap) -> None:
    """Set atmosphere properties from worldspawn onto LvlFile."""
    atmos = extract_atmosphere(parsed_map.entities)

    def intern(s: str) -> int:
        if s not in lvl.strings:
            lvl.strings.append(s)
        return lvl.strings.index(s)

    lvl.skybox_str_id = intern(atmos["skybox"]) if atmos["skybox"] else 0
    lvl.music_str_id = intern(atmos["music"]) if atmos["music"] else 0
    lvl.ambience_str_id = intern(atmos["ambience"]) if atmos["ambience"] else 0
    lvl.snow_amount_q8 = max(0, min(65535, int(atmos["snow_amount"] * 256.0)))
    sd = atmos["snow_dir"]
    lvl.snow_dir = (
        int(sd[0]), int(sd[1]), int(sd[2])
    )


def write_lvl(
    parsed_map: ParsedMap,
    out_path: str,
    scale: float = 0.2,
    eps: float = 1e-4,
    strict: bool = False
) -> LvlStats:
    """Write LVL2 file from ParsedMap.

    Args:
        parsed_map: ParsedMap to convert
        out_path: Output .lvl file path
        scale: World scale factor
        eps: Tolerance for geometry operations
        strict: If True, fail on invalid brushes

    Returns:
        LvlStats with generation statistics
    """
    stats = LvlStats()

    # Build material manifest
    manifest = build_material_manifest(parsed_map.entities)

    # Create LvlFile
    lvl = LvlFile()
    lvl.strings = list(manifest)

    # Build faces and vertices
    build_lvl_faces(parsed_map, manifest, scale, lvl)

    # Build entity spawns
    entity_count = build_entity_spawns(parsed_map, scale, lvl)

    # Apply atmosphere
    apply_atmosphere(lvl, parsed_map)

    # Update stats
    stats.faces = len(lvl.faces)
    stats.vertices = len(lvl.vertices)
    stats.entities = len(lvl.entities)

    # Write file
    lvl.write(out_path)

    return stats
