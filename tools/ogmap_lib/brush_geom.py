#!/usr/bin/env python3
"""
Brush geometry operations for OG Map processing.

Contains Sutherland-Hodgman polygon clipping, face polygon computation,
vertex sorting, deduplication, and fan triangulation.
"""

import math
from typing import List, Tuple, Dict, Optional

# Import Vec3 from parent package
from . import Vec3, vadd, vsub, vscale, vdot, vcross, vlength, vnormalize


def clip_polygon_by_plane(
    verts: List[Vec3],
    normal: Vec3,
    dist: float,
    eps: float = 0.01
) -> List[Vec3]:
    """Clip a convex polygon against a plane (keeps the front/solid side).

    The plane equation is: dot(normal, point) + dist >= 0 for the kept side.

    Args:
        verts: Polygon vertices in order
        normal: Plane normal (pointing toward kept side)
        dist: Plane distance term (plane eq: dot(normal, point) + dist = 0)
        eps: Tolerance for point-on-plane test

    Returns:
        Clipped polygon vertices, or empty list if fully clipped
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


def compute_face_polygon(
    brush_planes: List[Dict],
    face_idx: int
) -> List[Vec3]:
    """Compute the convex polygon for brush_planes[face_idx] by clipping
    all other planes against it.

    Uses Sutherland-Hodgman algorithm: start with a large base quad on the
    face plane, then clip against each other plane in the brush.

    Args:
        brush_planes: List of face dicts, each with 'normal' and 'dist'
        face_idx: Index of the face to compute polygon for

    Returns:
        List of vertices forming the convex polygon, or empty if degenerate
    """
    face = brush_planes[face_idx]
    face_normal = face["normal"]

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

    # Clip against all other planes in the brush
    # Interior is defined by: dot(normal, v) + dist >= 0 for all planes
    polygon = corners
    for i, plane in enumerate(brush_planes):
        if i == face_idx:
            continue
        polygon = clip_polygon_by_plane(polygon, plane["normal"], plane["dist"])
        if len(polygon) < 3:
            return []

    return polygon


def sort_vertices_ccw(
    vertices: List[Vec3],
    normal: Vec3
) -> List[Vec3]:
    """Sort convex polygon vertices CCW around the face normal (viewed from outside).

    Args:
        vertices: Polygon vertices in arbitrary order
        normal: Face normal (pointing outward from brush)

    Returns:
        Vertices sorted CCW around the normal
    """
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

    # The local basis above yields the opposite winding for our transformed
    # face normals, so flip the sorted order before the polygon is fanned into
    # render triangles.
    return list(reversed(sorted(vertices, key=angle_key)))


def dedupe_polygon_vertices(
    vertices: List[Vec3],
    eps: float = 1e-4
) -> List[Vec3]:
    """Drop consecutive duplicate vertices introduced by plane clipping.

    Args:
        vertices: Polygon vertices (possibly with duplicates)
        eps: Distance threshold for considering vertices equal

    Returns:
        Deduplicated vertices, preserving order
    """
    if len(vertices) < 2:
        return vertices

    out: List[Vec3] = []
    eps2 = eps * eps

    def is_same(a: Vec3, b: Vec3) -> bool:
        dx = a[0] - b[0]
        dy = a[1] - b[1]
        dz = a[2] - b[2]
        return (dx * dx) + (dy * dy) + (dz * dz) <= eps2

    for v in vertices:
        if not out or not is_same(v, out[-1]):
            out.append(v)

    if len(out) > 1 and is_same(out[0], out[-1]):
        out.pop()

    return out


def fan_triangulate(verts: List[Vec3]) -> List[Tuple[int, int, int]]:
    """Fan-triangulate a convex polygon into (i0,i1,i2) index tuples.

    Args:
        verts: Vertices in CCW order

    Returns:
        List of triangle index tuples
    """
    n = len(verts)
    return [(0, i, i + 1) for i in range(1, n - 1)]


def validate_brush_closed(
    brush: 'Brush',
    eps: float = 1e-4
) -> Tuple[bool, List[str]]:
    """Validate that a brush is topologically closed.

    Checks:
    - At least four faces
    - Each face has at least three vertices after dedupe
    - No degenerate faces
    - Each topological edge shared exactly twice

    Args:
        brush: Brush to validate
        eps: Distance tolerance for vertex comparison

    Returns:
        (is_valid, list_of_diagnostic_messages)
    """
    messages = []

    if len(brush.faces) < 4:
        messages.append(f"brush has only {len(brush.faces)} faces (need >= 4)")
        return (False, messages)

    # Compute face polygons
    face_polygons = []
    for i, face in enumerate(brush.faces):
        poly = compute_face_polygon(brush.faces, i)
        poly = dedupe_polygon_vertices(poly, eps)
        if len(poly) < 3:
            messages.append(f"face {i} degenerate after clipping (only {len(poly)} verts)")
            return (False, messages)
        face_polygons.append(poly)

    # Check edge sharing: each edge should appear exactly twice (once per direction)
    edge_count: Dict[Tuple[int, int], int] = {}
    eps2 = eps * eps

    def vertex_key(v: Vec3) -> Tuple[int, int, int]:
        """Quantize vertex for edge comparison."""
        return (int(v[0] / eps), int(v[1] / eps), int(v[2] / eps))

    for poly in face_polygons:
        n = len(poly)
        for j in range(n):
            v0 = vertex_key(poly[j])
            v1 = vertex_key(poly[(j + 1) % n])
            # Store edge with consistent ordering (smaller key first)
            edge = (min(v0, v1), max(v0, v1))
            edge_count[edge] = edge_count.get(edge, 0) + 1

    # Check that all edges are shared exactly twice
    invalid_edges = [(edge, count) for edge, count in edge_count.items() if count != 2]
    if invalid_edges:
        messages.append(f"brush has {len(invalid_edges)} edges not shared exactly twice")
        # Add details for first few invalid edges
        for edge, count in invalid_edges[:3]:
            messages.append(f"  edge {edge}: shared {count} times")

    return (len(messages) == 0, messages)


def validate_scene(
    parsed_map: 'ParsedMap',
    eps: float = 1e-4,
    strict: bool = False
) -> Tuple[bool, List[str]]:
    """Validate all brushes in a scene.

    Args:
        parsed_map: ParsedMap to validate
        eps: Distance tolerance for vertex comparison
        strict: If True, invalid brushes abort; if False, they're omitted with warning

    Returns:
        (all_valid, list_of_diagnostic_messages)
    """
    messages = []
    invalid_brushes = []

    for ent_idx, ent in enumerate(parsed_map.entities):
        for brush_idx, brush in enumerate(ent.brushes):
            is_valid, brush_messages = validate_brush_closed(brush, eps)
            if not is_valid:
                msg = f"entity {ent_idx} ({ent.classname}), brush {brush_idx}: {'; '.join(brush_messages)}"
                messages.append(msg)
                invalid_brushes.append((ent_idx, brush_idx))

    if strict and invalid_brushes:
        return (False, messages)

    return (len(invalid_brushes) == 0, messages)
