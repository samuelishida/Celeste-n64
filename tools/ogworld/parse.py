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
import itertools
import math
from pathlib import Path
from typing import List, Dict, Tuple, Optional

from .model import (
    WorldBuild, SourceBrush, SourceFace, SpawnRecord, HazardRecord,
    DynamicSolidRecord, DYNAMIC_SOLID_KIND,
    DYNAMIC_SOLID_MISSING_TARGET,
    MAT_SOLID, MAT_ONEWAY, MAT_DEATH, MAT_CLIMBABLE,
)
from .class_policy import (
    policy_for, is_skipped, validate_policies, summarize_policies,
    COLLISION_SOLID, COLLISION_ONEWAY, COLLISION_CLIMBABLE, COLLISION_NONE,
    RENDER_STATIC, RENDER_ACTOR, RENDER_NONE,
    DYNAMIC_STATIC_PROXY,
)

# Reuse the proven parser + geometry primitives.
from ogmap_lib import parse_map, transform_point, transform_normal
from ogmap_lib.brush_geom import (
    compute_face_polygon, sort_vertices_ccw, dedupe_polygon_vertices, outward_face_normal,
    fan_triangulate,
)
from ogmap_lib.texture_mapping import compute_uv
from asset_manifest import load_manifest, texture_profiles


def _sha256(path: str) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(8192), b""):
            h.update(chunk)
    return h.hexdigest()


def _policy_attr(policy, attr, default):
    """Read a class-policy attribute, falling back to `default` when unset."""
    return getattr(policy, attr) if policy is not None else default


def _collision_mode(policy) -> str:
    return _policy_attr(policy, "collision_mode", COLLISION_NONE)


def _render_mode(policy) -> str:
    return _policy_attr(policy, "render_mode", RENDER_NONE)


def _material_flags(policy) -> int:
    return _policy_attr(policy, "material_flags", 0)


def _resolve_dynamic_target(entities, target_name: str, scale: float):
    """Match OG Map.FindEntity: first source-order targetname, any class."""
    if not target_name:
        return DYNAMIC_SOLID_MISSING_TARGET, (0.0, 0.0, 0.0)
    for source_id, entity in enumerate(entities):
        if entity.properties.get("targetname", "") == target_name:
            return source_id, transform_point(entity.origin, scale)
    return DYNAMIC_SOLID_MISSING_TARGET, (0.0, 0.0, 0.0)


def _face_filter(policy) -> str:
    if policy is None:
        return "none"
    return policy.face_filter


def build_world_ir(
    map_path: str,
    scale: float = 0.2,
    eps: float = 1e-4,
    strict: bool = True,
    asset_manifest_path: str | None = None,
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

    manifest_path = Path(asset_manifest_path) if asset_manifest_path else (
        Path(__file__).resolve().parents[2] / "assets/og_converted/asset_manifest.json"
    )
    try:
        asset_registry = load_manifest(manifest_path)
        texture_profile_map = texture_profiles(asset_registry, manifest_path.parents[2])
    except (OSError, ValueError) as exc:
        if strict:
            raise ValueError(f"texture asset manifest unavailable: {exc}") from exc
        texture_profile_map = {}
    missing_profiles = [
        tex for tex in texture_manifest
        if tex not in texture_profile_map
        and tex not in ("__TB_empty", "invisible")
    ]
    if missing_profiles and strict:
        raise ValueError("missing texture profiles: " + ", ".join(missing_profiles))

    build = WorldBuild(
        texture_manifest=texture_manifest,
        texture_profiles=texture_profile_map,
        source_sha256=_sha256(map_path),
        source_path=map_path,
        scale=scale,
        chunk_size=0.0,  # set by the caller (Inc 3)
        policy_summary=summarize_policies(),
    )

    # Preserve actor-solid identity and typed source parameters. OG resolves
    # targetname by first recursive source-order match; missing links return
    # world origin rather than failing map load.
    for ei, ent in enumerate(pm.entities):
        policy = policy_for(ent.classname)
        if not ent.brushes or policy is None or policy.dynamic != DYNAMIC_STATIC_PROXY:
            continue
        kind = DYNAMIC_SOLID_KIND.get(ent.classname)
        if kind is None:
            raise ValueError(f"dynamic-solid class lacks runtime kind: {ent.classname}")

        flags = 0
        if ent.classname == "MovingBlock" and _parse_int_property(ent.properties, "slow", 0) > 0:
            flags |= 0x01
        if ent.classname == "FallingBlock" and _parse_int_property(ent.properties, "secret", 0) != 0:
            flags |= 0x01

        target_name = ent.properties.get("target", "")
        target_source_id, target_position = _resolve_dynamic_target(
            pm.entities, target_name, scale)

        build.dynamic_solids.append(DynamicSolidRecord(
            source_id=ei,
            classname=ent.classname,
            kind=kind,
            yaw_degrees=_parse_int_property(ent.properties, "angle", 0),
            flags=flags,
            target_source_id=target_source_id,
            target_position=target_position,
        ))

    # OG Map.LoadActor uses the first child Solid as LocalBounds for these
    # actors, then Actor.WorldBounds transforms all eight local AABB corners
    # by the entity angle. Preserve that volume independently of face policy.
    for ei, ent in enumerate(pm.entities):
        if ent.classname not in ("DeathBlock", "SpikeBlock"):
            continue
        if not ent.brushes:
            raise ValueError(f"{ent.classname} entity {ei} has no child solid for bounds")
        first_solid = ent.brushes[0]
        source_points = []
        for face_index in range(len(first_solid.faces)):
            source_points.extend(compute_face_polygon(first_solid.faces, face_index))
        if not source_points:
            raise ValueError(f"{ent.classname} entity {ei} has empty child-solid bounds")

        points = [transform_point(point, scale) for point in source_points]
        base_min = tuple(min(point[axis] for point in points) for axis in range(3))
        base_max = tuple(max(point[axis] for point in points) for axis in range(3))
        center = tuple((base_min[axis] + base_max[axis]) * 0.5 for axis in range(3))
        local_min = tuple(base_min[axis] - center[axis] for axis in range(3))
        local_max = tuple(base_max[axis] - center[axis] for axis in range(3))
        angle = math.radians(_parse_int_property(ent.properties, "angle", 0))
        cosine, sine = math.cos(angle), math.sin(angle)

        def rotate_local(point):
            return (
                center[0] + cosine * point[0] + sine * point[2],
                center[1] + point[1],
                center[2] - sine * point[0] + cosine * point[2],
            )

        corners = [
            rotate_local((x, y, z))
            for x, y, z in itertools.product(
                (local_min[0], local_max[0]),
                (local_min[1], local_max[1]),
                (local_min[2], local_max[2]),
            )
        ]
        bounds_min = tuple(min(point[axis] for point in corners) for axis in range(3))
        bounds_max = tuple(max(point[axis] for point in corners) for axis in range(3))

        direction = (0.0, 0.0, 0.0)
        if ent.classname == "SpikeBlock":
            # SpikeBlock.Added chooses the thinnest LocalBounds axis in OG
            # coordinates (X, Y, Z); Quake->port maps those to X, -Z, Y.
            size_x = local_max[0] - local_min[0]
            size_y = local_max[2] - local_min[2]
            size_z = local_max[1] - local_min[1]
            if size_z < size_x and size_z < size_y:
                direction = (0.0, 1.0, 0.0)
            elif size_x < size_y:
                direction = (-1.0, 0.0, 0.0)
            else:
                direction = (0.0, 0.0, 1.0)
            direction = (
                cosine * direction[0] + sine * direction[2],
                direction[1],
                -sine * direction[0] + cosine * direction[2],
            )

        build.hazards.append(HazardRecord(
            kind="death" if ent.classname == "DeathBlock" else "spike",
            source_id=ei,
            bounds_min=bounds_min,
            bounds_max=bounds_max,
            spike_direction=direction,
        ))

    # Source brushes/faces with identity + policy.
    for ei, ent in enumerate(pm.entities):
        policy = policy_for(ent.classname)
        skip_reason = is_skipped(ent.classname)
        for bi, brush in enumerate(ent.brushes):
            faces: List[SourceFace] = []
            for fi, face in enumerate(brush.faces):
                # Upward-only filter for DEATH surfaces.
                if _face_filter(policy) == "upward":
                    n = transform_normal(outward_face_normal(face))
                    if n[1] <= 0.3:
                        continue
                faces.append(SourceFace(
                    entity_index=ei,
                    brush_index=bi,
                    face_index=fi,
                    classname=ent.classname,
                    texture=face["texture"],
                    normal=transform_normal(outward_face_normal(face)),
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
                    uv_scale_u=texture_profile_map.get(face["texture"], {}).get("uv_scale_u", 1.0),
                    uv_scale_v=texture_profile_map.get(face["texture"], {}).get("uv_scale_v", 1.0),
                ))
            build.source_brushes.append(SourceBrush(
                entity_index=ei,
                brush_index=bi,
                classname=ent.classname,
                entity_origin=ent.origin,
                has_entity_origin="origin" in ent.properties,
                entity_angle_degrees=_parse_int_property(ent.properties, "angle", 0),
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


def _parse_int_property(properties: Dict[str, str], name: str, default: int) -> int:
    """Match OG GetIntProperty's fallback for entity transform integers."""
    try:
        return int(properties[name])
    except (KeyError, TypeError, ValueError):
        return default
