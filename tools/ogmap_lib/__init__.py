#!/usr/bin/env python3
"""
OG Map Library — shared types, parser, class registry, and pipeline helpers.

Library-first architecture: thin baker scripts import from this package.
Parses OG Quake .map files and provides a clean scene representation
with entity class mapping via CLASS_REGISTRY.

Package structure:
- ogmap_lib.brush_geom: polygon clipping, face computation, vertex sorting
- ogmap_lib.texture_mapping: UV coordinate computation
- ogmap_lib (this module): types, parser, class registry, transforms
"""

import sys
import math
import re
import struct
from collections import Counter
from typing import List, Tuple, Dict, Set, Optional, Any, NamedTuple
from pathlib import Path
from enum import IntEnum

# ── Vec3 math (local to package) ───────────────────────────────────

Vec3 = Tuple[float, float, float]


def vadd(a: Vec3, b: Vec3) -> Vec3:
    return (a[0] + b[0], a[1] + b[1], a[2] + b[2])


def vsub(a: Vec3, b: Vec3) -> Vec3:
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def vscale(a: Vec3, s: float) -> Vec3:
    return (a[0] * s, a[1] * s, a[2] * s)


def vdot(a: Vec3, b: Vec3) -> float:
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def vcross(a: Vec3, b: Vec3) -> Vec3:
    return (
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    )


def vlength(a: Vec3) -> float:
    return math.sqrt(vdot(a, a))


def vnormalize(a: Vec3) -> Vec3:
    l = vlength(a)
    return vscale(a, 1.0 / l) if l > 1e-10 else (0, 0, 1)


# ── Import geometry helpers from submodules ─────────────────────────

from .brush_geom import (
    clip_polygon_by_plane,
    compute_face_polygon,
    sort_vertices_ccw,
    dedupe_polygon_vertices,
    fan_triangulate,
    validate_brush_closed,
    validate_scene,
)
from .texture_mapping import compute_uv

# ── Types ───────────────────────────────────────────────────────────

FaceDef = Dict[str, Any]


class Brush:
    """A convex brush defined by a list of face planes."""
    def __init__(self, faces: List[FaceDef], valid: bool = True, diagnostics: Optional[List[str]] = None):
        self.faces = faces
        self.valid = valid  # False if brush is topologically invalid
        self.diagnostics = diagnostics or []  # List of diagnostic messages

    def __repr__(self):
        status = "valid" if self.valid else "INVALID"
        return f"Brush({len(self.faces)} faces, {status})"


class Entity:
    """A parsed entity from a Quake .map file."""
    def __init__(self, classname: str = "", origin: Vec3 = (0, 0, 0)):
        self.classname = classname
        self.origin = origin
        self.properties: Dict[str, str] = {}
        self.brushes: List[Brush] = []

    def __repr__(self):
        return f"Entity({self.classname}, {len(self.brushes)} brushes)"


class ParsedMap(NamedTuple):
    entities: List[Entity]
    textures: List[str]  # unique texture names
    world_range: Tuple[Vec3, Vec3]  # (min, max) in Quake coords


# ── Class system ────────────────────────────────────────────────────

class MaterialClass(IntEnum):
    SOLID = 0       # MAT_SOLID (0x0001)
    DEATH = 1       # MAT_SOLID | MAT_DEATH (0x0005)
    CLIMBABLE = 2   # MAT_SOLID | MAT_CLIMBABLE (0x0009)
    VISUAL_ONLY = 4 # 0 — not in colmesh
    TRIGGER = 5     # 0 — non-solid trigger volume
    ONEWAY = 6      # MAT_SOLID | MAT_ONEWAY (0x0003)
    # ICE (3) reserved for future


class EntityClass(IntEnum):
    NONE = -1           # brush-only, no spawn
    PLAYER_SPAWN = 0
    STRAWBERRY = 1
    REFILL = 2
    SPRING = 3
    CASSETTE = 9
    TRAFFIC_BLOCK = 100  # moving platform (needs .nav)


class RenderMode(IntEnum):
    NONE = 0           # invisible (triggers, spawn points)
    STATIC_MESH = 1    # included in .t3dm + .lvl
    ACTOR_MODEL = 2    # separate .t3dm, not baked into room mesh


class FaceFilter(IntEnum):
    NONE = 0           # emit all faces
    UPWARD_ONLY = 1    # only faces with game-space normal y > 0.3


class CollisionMode(IntEnum):
    NONE = 0           # no collision geometry
    SOLID = 1          # solid collision (default for worldspawn)
    ONEWAY = 2         # one-way platform (jump through from below)
    CLIMBABLE = 3      # climbable wall


class ClassDef(NamedTuple):
    material_class: MaterialClass
    entity_class: EntityClass
    render_mode: RenderMode
    face_filter: FaceFilter
    collision_mode: CollisionMode = CollisionMode.SOLID  # default: solid


CLASS_REGISTRY: Dict[str, ClassDef] = {
    "worldspawn":            ClassDef(MaterialClass.SOLID,       EntityClass.NONE,         RenderMode.STATIC_MESH, FaceFilter.NONE,         CollisionMode.SOLID),
    "PlayerSpawn":           ClassDef(MaterialClass.VISUAL_ONLY, EntityClass.PLAYER_SPAWN, RenderMode.NONE,        FaceFilter.NONE,         CollisionMode.NONE),
    "Strawberry":            ClassDef(MaterialClass.VISUAL_ONLY, EntityClass.STRAWBERRY,   RenderMode.NONE,        FaceFilter.NONE,         CollisionMode.NONE),
    "Refill":                ClassDef(MaterialClass.VISUAL_ONLY, EntityClass.REFILL,       RenderMode.NONE,        FaceFilter.NONE,         CollisionMode.NONE),
    "Spring":                ClassDef(MaterialClass.VISUAL_ONLY, EntityClass.SPRING,       RenderMode.NONE,        FaceFilter.NONE,         CollisionMode.NONE),
    "Cassette":              ClassDef(MaterialClass.VISUAL_ONLY, EntityClass.CASSETTE,     RenderMode.NONE,        FaceFilter.NONE,         CollisionMode.NONE),
    "SpikeBlock":            ClassDef(MaterialClass.DEATH,       EntityClass.NONE,         RenderMode.STATIC_MESH, FaceFilter.UPWARD_ONLY, CollisionMode.SOLID),
    "DeathBlock":            ClassDef(MaterialClass.DEATH,       EntityClass.NONE,         RenderMode.STATIC_MESH, FaceFilter.UPWARD_ONLY, CollisionMode.SOLID),
    "Decoration":            ClassDef(MaterialClass.VISUAL_ONLY, EntityClass.NONE,         RenderMode.STATIC_MESH, FaceFilter.NONE,         CollisionMode.NONE),
    "FloatingDecoration":    ClassDef(MaterialClass.VISUAL_ONLY, EntityClass.NONE,         RenderMode.STATIC_MESH, FaceFilter.NONE,         CollisionMode.NONE),
    "TrafficBlock":          ClassDef(MaterialClass.SOLID,       EntityClass.TRAFFIC_BLOCK, RenderMode.STATIC_MESH, FaceFilter.NONE,         CollisionMode.SOLID),
    "FallingBlock":          ClassDef(MaterialClass.SOLID,       EntityClass.NONE,         RenderMode.STATIC_MESH, FaceFilter.NONE,         CollisionMode.SOLID),
    "FloatyBlock":           ClassDef(MaterialClass.SOLID,       EntityClass.NONE,         RenderMode.STATIC_MESH, FaceFilter.NONE,         CollisionMode.SOLID),
    "GateBlock":             ClassDef(MaterialClass.SOLID,       EntityClass.NONE,         RenderMode.STATIC_MESH, FaceFilter.NONE,         CollisionMode.SOLID),
    "MovingBlock":           ClassDef(MaterialClass.SOLID,       EntityClass.NONE,         RenderMode.STATIC_MESH, FaceFilter.NONE,         CollisionMode.SOLID),
    "CassetteBlock":         ClassDef(MaterialClass.SOLID,       EntityClass.NONE,         RenderMode.STATIC_MESH, FaceFilter.NONE,         CollisionMode.SOLID),
    "BreakBlock":            ClassDef(MaterialClass.SOLID,       EntityClass.NONE,         RenderMode.STATIC_MESH, FaceFilter.NONE,         CollisionMode.SOLID),
    "DoubleDashPuzzleBlock": ClassDef(MaterialClass.SOLID,       EntityClass.NONE,         RenderMode.STATIC_MESH, FaceFilter.NONE,         CollisionMode.SOLID),
}

SKIPPED_CLASSES: Dict[str, str] = {
    "Node":         "pathfinding node, no geometry",
    "func_group":   "TrenchBroom layer container, no geometry",
    "StaticProp":   "visual-only point entity, no collision",
    "Coin":         "needs coin runtime (future)",
    "Feather":      "needs feather runtime (future)",
    "SignPost":     "needs sign/dialog runtime (future)",
    "IntroCar":     "cutscene entity, no geometry",
    "Granny":       "NPC, no geometry",
    "Theo":         "NPC, no geometry",
    "Badeline":     "NPC, no geometry",
    "Chimney":      "needs chimney runtime (future)",
    "FixedCamera":  "camera hint, no geometry",
    "EndingArea":   "trigger volume, no geometry",
}

# Material flag bits (matching coll_mesh.hpp)
MAT_SOLID = 0x0001
MAT_ONEWAY = 0x0002
MAT_DEATH = 0x0004
MAT_CLIMBABLE = 0x0008
MAT_ICE = 0x0010

# Colmesh triangle type: (i0, i1, i2, material_flags, face_id)
ColmeshTriangle = Tuple[int, int, int, int, int]


def material_class_to_flags(mc: MaterialClass) -> int:
    if mc == MaterialClass.DEATH:
        return MAT_SOLID | MAT_DEATH
    elif mc == MaterialClass.CLIMBABLE:
        return MAT_SOLID | MAT_CLIMBABLE
    elif mc == MaterialClass.ONEWAY:
        return MAT_SOLID | MAT_ONEWAY
    elif mc == MaterialClass.TRIGGER:
        return 0
    elif mc == MaterialClass.VISUAL_ONLY:
        return 0
    else:
        return MAT_SOLID


def classify_entity(ent: Entity) -> Optional[ClassDef]:
    """Look up an entity's ClassDef from the registry."""
    return CLASS_REGISTRY.get(ent.classname)


def is_skipped(ent: Entity) -> Optional[str]:
    """Return skip reason if the entity class is in SKIPPED_CLASSES."""
    return SKIPPED_CLASSES.get(ent.classname)


# ── Quake .map parser ───────────────────────────────────────────────

_FACE_PATTERN = re.compile(
    r'\(\s*([-\d.]+)\s+([-\d.]+)\s+([-\d.]+)\s*\)\s*'
    r'\(\s*([-\d.]+)\s+([-\d.]+)\s+([-\d.]+)\s*\)\s*'
    r'\(\s*([-\d.]+)\s+([-\d.]+)\s+([-\d.]+)\s*\)\s*'
    r'(\S+)\s+([-\d.]+)\s+([-\d.]+)\s+([-\d.]+)\s+([-\d.]+)\s+([-\d.]+)'
)


def parse_map(path: str) -> ParsedMap:
    """Parse a Standard Quake .map file into a ParsedMap."""
    with open(path, "r") as f:
        text = f.read()

    # Strip comments
    lines = []
    in_block_comment = False
    for line in text.split("\n"):
        stripped = line.strip()
        if "/*" in stripped:
            in_block_comment = True
        if "*/" in stripped:
            in_block_comment = False
            continue
        if in_block_comment:
            continue
        idx = stripped.find("//")
        if idx >= 0:
            stripped = stripped[:idx]
        lines.append(stripped)
    clean = "\n".join(lines)

    # Parse top-level { } blocks as entities
    entities: List[Entity] = []
    depth = 0
    start = -1
    for i, ch in enumerate(clean):
        if ch == "{":
            if depth == 0:
                start = i
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0 and start >= 0:
                block = clean[start:i + 1]
                entity = _parse_entity(block)
                entities.append(entity)
                start = -1

    # Collect unique textures and coordinate range
    textures: List[str] = []
    seen: Set[str] = set()
    all_coords: List[Vec3] = []

    for ent in entities:
        for brush in ent.brushes:
            for face in brush.faces:
                tex = face["texture"]
                if tex not in seen:
                    seen.add(tex)
                    textures.append(tex)
                all_coords.extend([face["p1"], face["p2"], face["p3"]])
        if ent.origin != (0, 0, 0):
            all_coords.append(ent.origin)

    world_range = ((0, 0, 0), (0, 0, 0))
    if all_coords:
        xs = [c[0] for c in all_coords]
        ys = [c[1] for c in all_coords]
        zs = [c[2] for c in all_coords]
        world_range = (
            (min(xs), min(ys), min(zs)),
            (max(xs), max(ys), max(zs)),
        )

    return ParsedMap(entities=entities, textures=textures, world_range=world_range)


def _parse_entity(block: str) -> Entity:
    entity = Entity()
    for match in re.finditer(r'"([^"]+)"\s+"([^"]*)"', block):
        key = match.group(1)
        value = match.group(2)
        entity.properties[key] = value

    entity.classname = entity.properties.get("classname", "unknown")
    origin_str = entity.properties.get("origin", "0 0 0")
    parts = origin_str.split()
    if len(parts) == 3:
        try:
            entity.origin = (float(parts[0]), float(parts[1]), float(parts[2]))
        except ValueError:
            entity.origin = (0, 0, 0)

    depth = -1
    brush_start = -1
    for i, ch in enumerate(block):
        if ch == "{":
            if depth == 0:
                brush_start = i
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0 and brush_start >= 0:
                brush_text = block[brush_start:i + 1]
                brush = _parse_brush(brush_text)
                if brush:
                    entity.brushes.append(brush)
                brush_start = -1

    return entity


def _parse_brush(brush_text: str) -> Brush:
    """Parse a brush block, preserving invalid brushes with diagnostics."""
    faces: List[FaceDef] = []
    diagnostics: List[str] = []

    for match in _FACE_PATTERN.finditer(brush_text):
        try:
            p1 = (float(match.group(1)), float(match.group(2)), float(match.group(3)))
            p2 = (float(match.group(4)), float(match.group(5)), float(match.group(6)))
            p3 = (float(match.group(7)), float(match.group(8)), float(match.group(9)))
            texture = match.group(10)
            normal = vnormalize(vcross(vsub(p2, p1), vsub(p3, p1)))
            dist = -vdot(normal, p1)
            faces.append({
                "normal": normal,
                "dist": dist,
                "p1": p1, "p2": p2, "p3": p3,
                "texture": texture,
                "shift_u": float(match.group(11)),
                "shift_v": float(match.group(12)),
                "rotation": float(match.group(13)),
                "scale_u": float(match.group(14)),
                "scale_v": float(match.group(15)),
            })
        except (ValueError, IndexError):
            diagnostics.append("failed to parse face")

    # Create brush even if invalid (< 4 faces)
    if len(faces) < 4:
        diagnostics.append(f"brush has only {len(faces)} faces (need >= 4)")

    return Brush(faces, valid=(len(faces) >= 4 and len(diagnostics) == 0), diagnostics=diagnostics)


# ── Coordinate transforms ───────────────────────────────────────────

def transform_point(p: Vec3, scale: float) -> Vec3:
    """Transform from Quake coords (Z-up) to port coords (Y-up)."""
    x, y, z = p
    return (x * scale, z * scale, -y * scale)


def transform_normal(n: Vec3) -> Vec3:
    """Transform normal from Quake to port coords (rotation only)."""
    x, y, z = n
    return (x, z, -y)


# ── Face filtering ─────────────────────────────────────────────────

def is_upward_face(face: FaceDef) -> bool:
    """Return True if the face's game-space normal points upward (y > 0.3)."""
    n = transform_normal(face["normal"])
    return n[1] > 0.3


# ── Material manifest ──────────────────────────────────────────────

def build_material_manifest(entities: List[Entity]) -> List[str]:
    """Collect unique texture names across all brush faces."""
    textures: List[str] = []
    seen: Set[str] = set()
    for ent in entities:
        for brush in ent.brushes:
            for face in brush.faces:
                tex = face["texture"]
                if tex not in seen:
                    seen.add(tex)
                    textures.append(tex)
    return textures


# ── Atmosphere extraction ──────────────────────────────────────────

def extract_atmosphere(entities: List[Entity]) -> Dict[str, Any]:
    """Extract atmosphere properties from the worldspawn entity."""
    props: Dict[str, Any] = {
        "skybox": "",
        "music": "",
        "ambience": "",
        "snow_amount": 0,
        "snow_dir": (0, 0, 0),
    }
    for ent in entities:
        if ent.classname == "worldspawn":
            props["skybox"] = ent.properties.get("skybox", "")
            props["music"] = ent.properties.get("music", "")
            props["ambience"] = ent.properties.get("ambience", "")
            try:
                props["snow_amount"] = float(ent.properties.get("snowAmount", "0"))
            except ValueError:
                props["snow_amount"] = 0.0
            sd = ent.properties.get("snowDirection", "0 0 0").split()
            if len(sd) == 3:
                try:
                    props["snow_dir"] = (float(sd[0]), float(sd[1]), float(sd[2]))
                except ValueError:
                    pass
            break
    return props


# ── CLI test ────────────────────────────────────────────────────────

def main():
    """Test: parse a .map and print summary."""
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <in.map>")
        sys.exit(1)

    pm = parse_map(sys.argv[1])
    print(f"ParsedMap: {len(pm.entities)} entities, {len(pm.textures)} textures")
    print(f"World range: {pm.world_range}")
    for ent in pm.entities[:5]:
        cd = classify_entity(ent)
        reason = is_skipped(ent)
        if cd:
            print(f"  {ent.classname}: {cd}")
        elif reason:
            print(f"  {ent.classname}: SKIPPED ({reason})")
        elif ent.brushes:
            print(f"  {ent.classname}: UNKNOWN (has brushes!)")
        else:
            print(f"  {ent.classname}: (point entity, no ClassDef)")

    atmos = extract_atmosphere(pm.entities)
    print(f"Atmosphere: {atmos}")


if __name__ == "__main__":
    main()
