#!/usr/bin/env python3
"""Parse an OG .map into the canonical world IR (Inc 2).

Adapts the proven `ogmap_lib.parse_map` output into the IR, preserving entity
properties such as spawn names and cassette targets. Does NOT normalize
entities into fake `func_wall` classes.

The existing `tools/ogmap_lib/__init__.py` parser and brush geometry functions
remain reusable primitives.
"""

from __future__ import annotations

import hashlib
from typing import List, Dict, Tuple, Optional

from .model import (
    WorldBuild, SourceBrush, SourceFace, SpawnRecord,
    MAT_SOLID, MAT_ONEWAY, MAT_DEATH, MAT_CLIMBABLE,
)
from .class_policy import (
    policy_for, is_skipped, validate_policies, summarize_policies,
    COLLISION_SOLID, COLLISION_ONEWAY, COLLISION_CLIMBABLE, COLLISION_NONE,
    RENDER_STATIC, RENDER_ACTOR, RENDER_NONE,
)

# Reuse the proven parser + geometry primitives.
from ogmap_lib import parse_map, transform_point, transform_normal
from ogmap_lib.brush_geom import (
    compute_face_polygon, sort_vertices_ccw, dedupe_polygon_vertices,
    fan_triangulate,
)
from ogmap_lib.texture_mapping import compute_uv


def _sha256(path: str) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(8192), b""):
            h.update(chunk)
    return h.hexdigest()


def _collision_mode(policy) -> str:
    if policy is None:
        return COLLISION_NONE
    return policy.collision_mode


def _render_mode(policy) -> str:
    if policy is None:
        return RENDER_NONE
    return policy.render_mode


def _material_flags(policy) -> int:
    if policy is None:
        return 0
    return policy.material_flags


def _face_filter(policy) -> str:
    if policy is None:
        return "none"
    return policy.face_filter


def build_world_ir(
    map_path: str,
    scale: float = 0.2,
    eps: float = 1e-4,
    strict: bool = True,
) -> WorldBuild:
    """Parse a .map into a deterministic WorldBuild IR.

    This builds the source-identity layer only: source brushes/faces and
    spawn records. It does NOT yet partition visual geometry or build global
    collision (that is Inc 3). The `chunks` and `collision_tris` fields are
    left empty here; `texture_manifest` and `spawns` are populated.

    Raises ValueError on unknown brush classes in strict mode.
    """
    pm = parse_map(map_path)

    # Validate class policy coverage.
    errors = validate_policies(pm, strict=strict)
    if errors:
        msg = "\n".join(errors)
        if strict:
            raise ValueError(f"unknown brush classes in {map_path}:\n{msg}")
        # Non-strict: still surface them in the report.

    # Texture manifest (ordered, deterministic).
    texture_manifest: List[str] = []
    seen: set = set()
    for ent in pm.entities:
        for brush in ent.brushes:
            for face in brush.faces:
                tex = face["texture"]
                if tex not in seen:
                    seen.add(tex)
                    texture_manifest.append(tex)

    build = WorldBuild(
        texture_manifest=texture_manifest,
        source_sha256=_sha256(map_path),
        source_path=map_path,
        scale=scale,
        chunk_size=0.0,  # set by the caller (Inc 3)
        policy_summary=summarize_policies(),
    )

    # Source brushes/faces with identity + policy.
    for ei, ent in enumerate(pm.entities):
        policy = policy_for(ent.classname)
        skip_reason = is_skipped(ent.classname)
        for bi, brush in enumerate(ent.brushes):
            faces: List[SourceFace] = []
            for fi, face in enumerate(brush.faces):
                # Upward-only filter for DEATH surfaces.
                if _face_filter(policy) == "upward":
                    n = transform_normal(face["normal"])
                    if n[1] <= 0.3:
                        continue
                faces.append(SourceFace(
                    entity_index=ei,
                    brush_index=bi,
                    face_index=fi,
                    classname=ent.classname,
                    texture=face["texture"],
                    normal=transform_normal(face["normal"]),
                    material_flags=_material_flags(policy),
                    collision_mode=_collision_mode(policy),
                    render_mode=_render_mode(policy),
                    src_p1=face["p1"],
                    src_p2=face["p2"],
                    src_p3=face["p3"],
                    shift_u=face.get("shift_u", 0.0),
                    shift_v=face.get("shift_v", 0.0),
                    rotation=face.get("rotation", 0.0),
                    scale_u=face.get("scale_u", 1.0),
                    scale_v=face.get("scale_v", 1.0),
                ))
            build.source_brushes.append(SourceBrush(
                entity_index=ei,
                brush_index=bi,
                classname=ent.classname,
                planes=tuple(brush.faces),
                faces=tuple(faces),
                valid=brush.valid,
                diagnostics=tuple(brush.diagnostics),
            ))
            # Record the brush (even if it has no policy — for diagnostics).
            # We keep it in the IR so Inc 3 can report unowned geometry.
            build.brush_cell_counts[(ei, bi)] = 0  # filled in Inc 3

        # Spawn records for point entities.
        if not ent.brushes:
            kind = "actor"
            if ent.classname == "PlayerSpawn":
                name = ent.properties.get("name", "")
                kind = "start" if name == "Start" else "anchor"
            elif ent.classname in ("Strawberry", "Refill", "Spring", "Cassette"):
                kind = "actor"
            else:
                # Non-actor point entities (Node, FixedCamera, etc.) are not
                # spawn records; skip them.
                continue
            build.spawns.append(SpawnRecord(
                kind=kind,
                source_id=ei,
                room_id="",
                position=transform_point(ent.origin, scale),
                name=ent.properties.get("name", ""),
                classname=ent.classname,
                properties=dict(ent.properties),
            ))

    return build
