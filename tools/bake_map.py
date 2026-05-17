#!/usr/bin/env python3
"""Bake Quake .map files to custom LVL binary format.

Each brush face becomes a separate LVL Face with its own convex polygon
computed by clipping all other brush planes against the face plane.
Vertices are sorted CCW around the face normal. UVs are derived from
the Quake face texture parameters (shift, rotation, scale).
"""

import sys
import math
import re
from typing import List, Tuple, Dict, Set, Optional
from pathlib import Path
import argparse

# Add tools to path for imports
sys.path.insert(0, str(Path(__file__).parent))

from lvl_format import LvlFile, Collider, Face, Vertex, Entity
from entity_ids import ENTITY_IDS, id_of

# ── Vector math helpers ──────────────────────────────────────────────

Vec3 = Tuple[float, float, float]

def vadd(a: Vec3, b: Vec3) -> Vec3:
    return (a[0]+b[0], a[1]+b[1], a[2]+b[2])

def vsub(a: Vec3, b: Vec3) -> Vec3:
    return (a[0]-b[0], a[1]-b[1], a[2]-b[2])

def vscale(a: Vec3, s: float) -> Vec3:
    return (a[0]*s, a[1]*s, a[2]*s)

def vdot(a: Vec3, b: Vec3) -> float:
    return a[0]*b[0] + a[1]*b[1] + a[2]*b[2]

def vcross(a: Vec3, b: Vec3) -> Vec3:
    return (
        a[1]*b[2] - a[2]*b[1],
        a[2]*b[0] - a[0]*b[2],
        a[0]*b[1] - a[1]*b[0],
    )

def vlength(a: Vec3) -> float:
    return math.sqrt(vdot(a, a))

def vnormalize(a: Vec3) -> Vec3:
    l = vlength(a)
    return vscale(a, 1.0/l) if l > 1e-10 else (0, 0, 1)

# ── Quake .map parser ────────────────────────────────────────────────

def parse_map_file(filename: str) -> List[Dict]:
    """Parse Standard Quake .map file into entity list."""
    with open(filename, "r") as f:
        content = f.read()

    # Strip comments
    lines = []
    in_block_comment = False
    for line in content.split("\n"):
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
    text = "\n".join(lines)

    # Parse top-level { ... } blocks as entities
    entities = []
    depth = 0
    start = -1
    for i, ch in enumerate(text):
        if ch == "{":
            if depth == 0:
                start = i
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0 and start >= 0:
                block = text[start:i+1]
                entity = _parse_entity_block(block)
                entities.append(entity)
                start = -1

    return entities


def _parse_entity_block(block: str) -> Dict:
    """Parse key-value pairs and brushes from a single entity block.

    The block text includes the entity's own { } wrapper.  Inside it,
    brush blocks are { plane lines } nested one level deep.
    """
    entity: Dict = {"brushes": []}

    # Extract key-value pairs
    for match in re.finditer(r'"([^"]+)"\s+"([^"]*)"', block):
        entity[match.group(1)] = match.group(2)

    # Find inner { } blocks (brushes) at depth 1.
    # Start depth at -1 so the entity's own { } pair is transparent;
    # blocks at depth 0 (relative) are the brush definitions.
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
                brush_text = block[brush_start:i+1]
                brush = _parse_brush(brush_text)
                if brush:
                    entity["brushes"].append(brush)
                brush_start = -1

    return entity


FaceDef = Dict  # TypedDict-style: normal, origin, texture, shift, rotation, scale

def _parse_brush(brush_text: str) -> List[FaceDef]:
    """Parse a brush block (inner { }) into a list of face definitions."""
    planes = []

    # Quake face format:
    # ( x1 y1 z1 ) ( x2 y2 z2 ) ( x3 y3 z3 ) texture_name shift_u shift_v rotation scale_u scale_v
    face_pattern = (
        r'\(\s*([-\d.]+)\s+([-\d.]+)\s+([-\d.]+)\s*\)\s*'
        r'\(\s*([-\d.]+)\s+([-\d.]+)\s+([-\d.]+)\s*\)\s*'
        r'\(\s*([-\d.]+)\s+([-\d.]+)\s+([-\d.]+)\s*\)\s*'
        r'(\S+)\s+([-\d.]+)\s+([-\d.]+)\s+([-\d.]+)\s+([-\d.]+)\s+([-\d.]+)'
    )

    for match in re.finditer(face_pattern, brush_text):
        try:
            p1 = (float(match.group(1)), float(match.group(2)), float(match.group(3)))
            p2 = (float(match.group(4)), float(match.group(5)), float(match.group(6)))
            p3 = (float(match.group(7)), float(match.group(8)), float(match.group(9)))
            texture = match.group(10)
            shift_u = float(match.group(11))
            shift_v = float(match.group(12))
            rotation = float(match.group(13))
            scale_u = float(match.group(14))
            scale_v = float(match.group(15))

            # Compute plane normal (Quake convention: outward-facing)
            v1 = vsub(p2, p1)
            v2 = vsub(p3, p1)
            normal = vcross(v1, v2)
            normal = vnormalize(normal)

            # Plane distance: -dot(normal, point_on_plane)
            dist = -vdot(normal, p1)

            planes.append({
                "normal": normal,
                "dist": dist,
                "p1": p1,
                "p2": p2,
                "p3": p3,
                "texture": texture,
                "shift_u": shift_u,
                "shift_v": shift_v,
                "rotation": rotation,
                "scale_u": scale_u,
                "scale_v": scale_v,
            })
        except (ValueError, IndexError):
            pass

    return planes if len(planes) >= 4 else []


# ── Brush → convex polygons (per-face clipping) ─────────────────────

def clip_polygon_by_plane(verts: List[Vec3], normal: Vec3, dist: float, eps: float = 0.01) -> List[Vec3]:
    """Clip a convex polygon against a plane (keeps the front/solid side).

    The plane equation is: dot(normal, point) + dist >= 0 for the kept side.
    """
    if len(verts) < 3:
        return []

    output = []
    prev_vert = verts[-1]
    prev_dot = vdot(normal, prev_vert) + dist

    for curr_vert in verts:
        curr_dot = vdot(normal, curr_vert) + dist

        if curr_dot >= -eps:
            # Current vertex is inside
            if prev_dot < -eps:
                # Previous was outside, compute intersection
                t = prev_dot / (prev_dot - curr_dot)
                intersection = vadd(prev_vert, vscale(vsub(curr_vert, prev_vert), t))
                output.append(intersection)
            output.append(curr_vert)
        elif prev_dot >= -eps:
            # Previous was inside, current outside, compute intersection
            t = prev_dot / (prev_dot - curr_dot)
            intersection = vadd(prev_vert, vscale(vsub(curr_vert, prev_vert), t))
            output.append(intersection)

        prev_vert = curr_vert
        prev_dot = curr_dot

    return output


def compute_face_polygon(brush_planes: List[FaceDef], face_idx: int) -> List[Vec3]:
    """Compute the convex polygon for brush_planes[face_idx] by clipping
    all other planes against it."""
    face = brush_planes[face_idx]
    face_normal = face["normal"]

    # Start with a large base polygon on the face plane
    # Find two tangent vectors perpendicular to the face normal
    if abs(face_normal[0]) < 0.9:
        tangent_u = vnormalize(vcross(face_normal, (1, 0, 0)))
    else:
        tangent_u = vnormalize(vcross(face_normal, (0, 1, 0)))
    tangent_v = vnormalize(vcross(face_normal, tangent_u))

    # Base polygon: large quad on the face plane
    # Use a large radius (8192 Quake units = ~163 port units)
    R = 8192.0
    center = vscale(face_normal, -face["dist"])
    corners = [
        vadd(center, vadd(vscale(tangent_u, R), vscale(tangent_v, R))),
        vadd(center, vadd(vscale(tangent_u, R), vscale(tangent_v, -R))),
        vadd(center, vadd(vscale(tangent_u, -R), vscale(tangent_v, -R))),
        vadd(center, vadd(vscale(tangent_u, -R), vscale(tangent_v, R))),
    ]

    # Clip against all other planes in the brush.
    # The cross-product normals point inward (toward the brush interior).
    # Interior is defined by: dot(normal, v) - d >= 0 for all planes, where d = -dist.
    # Equivalently: dot(normal, v) + dist >= 0 (since dist = -d).
    # clip_polygon_by_plane keeps the dot(normal, v) + dist >= 0 side,
    # so we pass the plane normal and dist directly.
    polygon = corners
    for i, plane in enumerate(brush_planes):
        if i == face_idx:
            continue
        polygon = clip_polygon_by_plane(polygon, plane["normal"], plane["dist"])
        if len(polygon) < 3:
            return []

    return polygon


def sort_vertices_ccw(vertices: List[Vec3], normal: Vec3) -> List[Vec3]:
    """Sort convex polygon vertices CCW around the face normal (viewed from outside)."""
    if len(vertices) < 3:
        return vertices

    # Project onto plane, compute centroid
    center = vscale(
        tuple(sum(v[i] for v in vertices) for i in range(3)),
        1.0 / len(vertices)
    )

    # Build local 2D coordinate system on the plane
    up = vnormalize(normal)
    if abs(up[0]) < 0.9:
        right = vnormalize(vcross(up, (1, 0, 0)))
    else:
        right = vnormalize(vcross(up, (0, 1, 0)))
    # Recalculate forward to be orthogonal
    forward = vcross(right, up)

    # Sort by angle around centroid
    def angle_key(v: Vec3) -> float:
        d = vsub(v, center)
        return math.atan2(vdot(d, forward), vdot(d, right))

    return sorted(vertices, key=angle_key)


def compute_uv(point: Vec3, face: FaceDef, scale: float = 0.02) -> Tuple[float, float]:
    """Compute UV coordinates from Quake face parameters.

    Quake UVs: project point onto the face's texture axis, apply shift/rotation/scale.
    Then transform to port coordinates.
    """
    # Reconstruct texture axes from the face's three points + rotation
    normal = face["normal"]
    p1 = face["p1"]

    rotation_deg = face["rotation"]
    scale_u = face["scale_u"] if face["scale_u"] != 0 else 1.0
    scale_v = face["scale_v"] if face["scale_v"] != 0 else 1.0
    shift_u = face["shift_u"]
    shift_v = face["shift_v"]

    # Determine base axis from dominant normal component
    # Quake convention for axis-aligned faces
    if abs(normal[0]) > abs(normal[1]) and abs(normal[0]) > abs(normal[2]):
        # X-normal face: U=Z, V=Y (or -Z depending on sign)
        if normal[0] > 0:
            axis_u = (0, 0, -1)  # -Z
            axis_v = (0, -1, 0)  # -Y
        else:
            axis_u = (0, 0, 1)   # Z
            axis_v = (0, -1, 0)   # -Y
    elif abs(normal[1]) > abs(normal[2]):
        # Y-normal face: U=X, V=Z
        if normal[1] > 0:
            axis_u = (1, 0, 0)
            axis_v = (0, 0, -1)
        else:
            axis_u = (1, 0, 0)
            axis_v = (0, 0, 1)
    else:
        # Z-normal face: U=X, V=-Y (Quake convention)
        if normal[2] > 0:
            axis_u = (1, 0, 0)
            axis_v = (0, -1, 0)
        else:
            axis_u = (1, 0, 0)
            axis_v = (0, 1, 0)

    # Apply rotation
    if rotation_deg != 0:
        rad = math.radians(rotation_deg)
        cos_r = math.cos(rad)
        sin_r = math.sin(rad)
        ou, ov = axis_u, axis_v
        axis_u = vadd(vscale(ou, cos_r), vscale(ov, -sin_r))
        axis_v = vadd(vscale(ou, sin_r), vscale(ov, cos_r))

    # Project point onto texture axes
    u = vdot(vsub(point, p1), axis_u)
    v = vdot(vsub(point, p1), axis_v)

    # Apply shift and scale
    u = (u + shift_u) / scale_u
    v = (v + shift_v) / scale_v

    # Convert to port units (divide by some world-scale factor to get reasonable tiling)
    # Port scale is 0.02 from Quake units to port units; texel density at ~16 px per port unit
    tex_per_unit = 0.003125  # 1/320: 320 quake units = 1 texture repeat at scale 1.0
    u *= tex_per_unit
    v *= tex_per_unit

    return (u, v)


# ── Coordinate transform ────────────────────────────────────────────

def transform_point(p: Tuple[float, float, float]) -> Tuple[float, float, float]:
    """Transform from Quake coords (Z-up) to port coords (Y-up), scale 0.02."""
    x, y, z = p
    return (x * 0.02, z * 0.02, -y * 0.02)

def transform_normal(n: Tuple[float, float, float]) -> Tuple[float, float, float]:
    """Transform normal from Quake to port coords (rotation only)."""
    x, y, z = n
    return (x, z, -y)


# ── Bake ────────────────────────────────────────────────────────────

def bake_map(map_file: str, lvl_file: str, manifest_file: str, dump_spawn: bool = False):
    """Bake .map file to .lvl binary and .manifest."""

    entities = parse_map_file(map_file)
    lvl = LvlFile()

    materials_used: Set[str] = set()
    spawn_coords = None

    # String intern table
    string_to_id: Dict[str, int] = {}
    def intern_string(s: str) -> int:
        if s not in string_to_id:
            string_to_id[s] = len(lvl.strings)
            lvl.strings.append(s)
        return string_to_id[s]

    # Process point entities (no brushes)
    for entity in entities:
        classname = entity.get("classname", "")
        if classname == "worldspawn":
            # Extract worldspawn properties
            if "skybox" in entity:
                lvl.skybox_str_id = intern_string(entity["skybox"])
            if "music" in entity:
                lvl.music_str_id = intern_string(entity["music"])
            if "ambience" in entity:
                lvl.ambience_str_id = intern_string(entity["ambience"])
            if "snowAmount" in entity:
                try:
                    amount = float(entity["snowAmount"])
                    lvl.snow_amount_q8 = int(amount * 256)
                except ValueError:
                    pass
            if "snowDirection" in entity:
                parts = entity["snowDirection"].split()
                if len(parts) == 3:
                    try:
                        nx, ny, nz = float(parts[0]), float(parts[1]), float(parts[2])
                        tx = transform_normal((nx, ny, nz))
                        lvl.snow_dir = (int(tx[0]*256), int(tx[1]*256), int(tx[2]*256))
                    except ValueError:
                        pass
            continue

        # Parse origin for point entities
        origin_str = entity.get("origin", "0 0 0")
        try:
            origin = tuple(float(x) for x in origin_str.split())
        except (ValueError, IndexError):
            origin = (0, 0, 0)

        transformed_origin = transform_point(origin)
        entity_id = id_of(classname)
        if entity_id is not None:
            ent = Entity(entity_id, transformed_origin)
            lvl.entities.append(ent)
            if classname == "PlayerSpawn":
                spawn_coords = transformed_origin
                if dump_spawn:
                    print(f"PlayerSpawn baked at ({transformed_origin[0]:.3f}, {transformed_origin[1]:.3f}, {transformed_origin[2]:.3f})")

    # Process brush geometry from all entities
    all_faces = 0
    all_verts = 0

    for entity in entities:
        brushes = entity.get("brushes", [])
        for brush in brushes:
            if len(brush) < 4:
                continue

            # Compute convex polygon for each face of the brush
            for face_idx in range(len(brush)):
                face_def = brush[face_idx]

                # Get the polygon for this face by clipping all other planes
                polygon = compute_face_polygon(brush, face_idx)
                if len(polygon) < 3:
                    continue

                # Sort vertices CCW around face normal
                polygon = sort_vertices_ccw(polygon, face_def["normal"])
                if len(polygon) < 3:
                    continue

                # Transform to port coordinates
                transformed_verts = [transform_point(v) for v in polygon]
                transformed_normal = transform_normal(face_def["normal"])

                # Compute UVs for each vertex
                raw_material_id = intern_string(face_def["texture"])
                materials_used.add(face_def["texture"])

                # Emit vertices with UVs
                vertex_start = len(lvl.vertices)
                for i, v in enumerate(polygon):
                    uv = compute_uv(v, face_def)
                    lvl.vertices.append(Vertex(transformed_verts[i], uv))

                # Emit face with raw material_id (will be remapped later)
                lvl.faces.append(Face(vertex_start, len(polygon), raw_material_id, transformed_normal))

                # Emit collider (AABB of the face polygon with skin thickness for raycast)
                # Add ±0.01 padding to give zero-thickness faces volume for AABB intersection
                bounds_min = tuple(min(v[i] for v in transformed_verts) - 0.01 for i in range(3))
                bounds_max = tuple(max(v[i] for v in transformed_verts) + 0.01 for i in range(3))
                collider = Collider(
                    type_=0,
                    solid=1,
                    has_plane_origin=1,
                    bounds_min=bounds_min,
                    bounds_max=bounds_max,
                    normal=transformed_normal,
                    plane_origin=transformed_verts[0],
                    face_id=len(lvl.faces) - 1,
                )
                lvl.colliders.append(collider)

                all_faces += 1
                all_verts += len(polygon)

    # Build material remap: string-table indices -> sequential material catalog slots
    # MaterialCatalog loads sprites at slots 0..N in manifest order, so we remap
    # raw string indices to sequential indices
    materials_in_order = [s for s in lvl.strings if s in materials_used]
    material_remap = {string_to_id[name]: i for i, name in enumerate(materials_in_order)}

    # Update all faces with remapped material_ids
    for face in lvl.faces:
        remapped_material_id = material_remap.get(face.material_id, 0)
        face.material_id = remapped_material_id

    # Write LVL file
    lvl.write(lvl_file)

    # Write manifest
    with open(manifest_file, "w") as f:
        for s in lvl.strings:
            if s in materials_used:
                f.write(s + "\n")

    print(f"face_count={len(lvl.faces)} vertex_count={len(lvl.vertices)} "
          f"collider_count={len(lvl.colliders)} entity_count={len(lvl.entities)}")
    print(f"materials: {', '.join(sorted(materials_used))}")
    print(f"wrote {lvl_file} ({len(open(lvl_file, 'rb').read())} bytes)")
    print(f"wrote {manifest_file}")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Bake Quake .map to LVL binary")
    parser.add_argument("map_file", help="Input .map file")
    parser.add_argument("lvl_file", help="Output .lvl file")
    parser.add_argument("manifest_file", help="Output .manifest file")
    parser.add_argument("--dump-spawn", action="store_true", help="Dump spawn coordinates")
    args = parser.parse_args()

    bake_map(args.map_file, args.lvl_file, args.manifest_file, args.dump_spawn)
