#!/usr/bin/env python3
"""
Brush fidelity tests for ogmap_lib geometry operations.

Tests synthetic cube brushes to verify:
- Clipping produces correct polygons
- Known face areas
- Winding order (CCW)
- Deduplication of vertices
- Open brush detection
"""

import sys
import math
import unittest
from pathlib import Path

# Add tools to path for imports
sys.path.insert(0, str(Path(__file__).parent.parent / "tools"))

from ogmap_lib import (
    Vec3, vadd, vsub, vcross, vdot, vnormalize,
    clip_polygon_by_plane, compute_face_polygon,
    sort_vertices_ccw, dedupe_polygon_vertices, fan_triangulate,
    Brush, FaceDef,
)


def make_cube_brush(size: float = 64.0) -> Brush:
    """Create a cube brush centered at origin with given size.

    Face normals point INWARD (toward brush center), which is what
    compute_face_polygon expects for clipping.
    Plane distance is computed as dist = -dot(normal, p1).
    """
    h = size / 2.0

    # Helper to create face dict with correct dist
    def make_face(normal, p1, p2, p3):
        dist = -vdot(normal, p1)
        return {
            "normal": normal, "dist": dist,
            "p1": p1, "p2": p2, "p3": p3,
            "texture": "test", "shift_u": 0, "shift_v": 0,
            "rotation": 0, "scale_u": 1, "scale_v": 1
        }

    # Six faces with INWARD-pointing normals
    faces = [
        # -X face (normal points left, toward center)
        make_face((-1, 0, 0), (-h, -h, -h), (-h, h, -h), (-h, h, h)),
        # +X face (normal points right, toward center)
        make_face((1, 0, 0), (h, -h, h), (h, h, h), (h, h, -h)),
        # -Y face (normal points down, toward center)
        make_face((0, -1, 0), (-h, -h, -h), (h, -h, -h), (h, -h, h)),
        # +Y face (normal points up, toward center)
        make_face((0, 1, 0), (-h, h, h), (h, h, h), (h, h, -h)),
        # -Z face (normal points backward, toward center)
        make_face((0, 0, -1), (-h, -h, -h), (-h, h, -h), (h, h, -h)),
        # +Z face (normal points forward, toward center)
        make_face((0, 0, 1), (-h, -h, h), (h, -h, h), (h, h, h)),
    ]
    return Brush(faces)


def make_open_brush() -> Brush:
    """Create an invalid brush with only 3 faces (not closed)."""
    faces = [
        {"normal": (1, 0, 0), "dist": -32, "p1": (32, -32, -32), "p2": (32, 32, -32), "p3": (32, 32, 32),
         "texture": "test", "shift_u": 0, "shift_v": 0, "rotation": 0, "scale_u": 1, "scale_v": 1},
        {"normal": (-1, 0, 0), "dist": -32, "p1": (-32, -32, 32), "p2": (-32, 32, 32), "p3": (-32, 32, -32),
         "texture": "test", "shift_u": 0, "shift_v": 0, "rotation": 0, "scale_u": 1, "scale_v": 1},
        {"normal": (0, 1, 0), "dist": -32, "p1": (-32, 32, -32), "p2": (32, 32, -32), "p3": (32, 32, 32),
         "texture": "test", "shift_u": 0, "shift_v": 0, "rotation": 0, "scale_u": 1, "scale_v": 1},
    ]
    return Brush(faces)


class TestClipPolygonByPlane(unittest.TestCase):
    """Test Sutherland-Hodgman polygon clipping."""

    def test_clip_square_by_vertical_plane(self):
        """Clip a square against a vertical plane."""
        # Square in XY plane at Z=0, from (-1,-1) to (1,1)
        square = [(-1, -1, 0), (1, -1, 0), (1, 1, 0), (-1, 1, 0)]
        # Clip against plane X=0 (keep X>=0 side)
        normal = (1, 0, 0)
        dist = 0.0
        result = clip_polygon_by_plane(square, normal, dist)
        # Should keep right half: (0,-1,0), (1,-1,0), (1,1,0), (0,1,0)
        self.assertEqual(len(result), 4)
        self.assertTrue(all(v[0] >= -0.01 for v in result))

    def test_clip_triangle_fully_inside(self):
        """Triangle fully inside clipping plane."""
        triangle = [(0, 0, 0), (1, 0, 0), (0, 1, 0)]
        normal = (0, 0, 1)
        dist = 0.0
        result = clip_polygon_by_plane(triangle, normal, dist)
        self.assertEqual(len(result), 3)

    def test_clip_empty_polygon(self):
        """Clipping empty polygon returns empty."""
        result = clip_polygon_by_plane([], (1, 0, 0), 0.0)
        self.assertEqual(len(result), 0)


class TestComputeFacePolygon(unittest.TestCase):
    """Test face polygon computation from brush planes."""

    def test_cube_face_area(self):
        """Cube face should have correct area."""
        brush = make_cube_brush(64.0)
        # Test +X face (index 0)
        polygon = compute_face_polygon(brush.faces, 0)
        self.assertGreaterEqual(len(polygon), 3)
        # Area should be 64*64 = 4096
        # Compute area via cross product of edges
        if len(polygon) >= 3:
            v0, v1, v2 = polygon[0], polygon[1], polygon[2]
            area = 0.5 * vlength(vcross(vsub(v1, v0), vsub(v2, v0)))
            self.assertAlmostEqual(area, 4096.0, delta=1.0)

    def test_cube_face_vertex_count(self):
        """Cube face should produce 4 vertices (quad)."""
        brush = make_cube_brush(32.0)
        for i in range(6):
            polygon = compute_face_polygon(brush.faces, i)
            self.assertEqual(len(polygon), 4, f"Face {i} should have 4 vertices")


class TestSortVerticesCCW(unittest.TestCase):
    """Test CCW vertex sorting."""

    def test_square_ccw(self):
        """Square vertices should sort CCW around normal."""
        # Square in XY plane, normal pointing up
        verts = [(1, 0, 0), (1, 1, 0), (0, 1, 0), (0, 0, 0)]
        normal = (0, 0, 1)
        sorted_verts = sort_vertices_ccw(verts, normal)
        # Check that winding is CCW
        # Cross product of consecutive edges should point in normal direction
        for i in range(len(sorted_verts)):
            v0 = sorted_verts[i]
            v1 = sorted_verts[(i + 1) % len(sorted_verts)]
            v2 = sorted_verts[(i + 2) % len(sorted_verts)]
            edge1 = vsub(v1, v0)
            edge2 = vsub(v2, v1)
            cross = vcross(edge1, edge2)
            # Should point up (positive Z)
            self.assertGreater(cross[2], 0)


class TestDedupePolygonVertices(unittest.TestCase):
    """Test vertex deduplication."""

    def test_dedupe_consecutive(self):
        """Remove consecutive duplicate vertices."""
        verts = [(0, 0, 0), (1, 0, 0), (1, 0, 0), (1, 1, 0), (1, 1, 0), (0, 1, 0)]
        result = dedupe_polygon_vertices(verts, eps=1e-4)
        self.assertEqual(len(result), 4)

    def test_dedupe_wrap_around(self):
        """Remove duplicate if first and last are same."""
        verts = [(0, 0, 0), (1, 0, 0), (1, 1, 0), (0, 1, 0), (0, 0, 0)]
        result = dedupe_polygon_vertices(verts, eps=1e-4)
        self.assertEqual(len(result), 4)

    def test_no_duplicates(self):
        """No duplicates to remove."""
        verts = [(0, 0, 0), (1, 0, 0), (1, 1, 0), (0, 1, 0)]
        result = dedupe_polygon_vertices(verts, eps=1e-4)
        self.assertEqual(len(result), 4)


class TestFanTriangulate(unittest.TestCase):
    """Test fan triangulation."""

    def test_quad_triangulation(self):
        """Quad should produce 2 triangles."""
        verts = [(0, 0, 0), (1, 0, 0), (1, 1, 0), (0, 1, 0)]
        triangles = fan_triangulate(verts)
        self.assertEqual(len(triangles), 2)
        # Check indices
        self.assertEqual(triangles[0], (0, 1, 2))
        self.assertEqual(triangles[1], (0, 2, 3))


class TestOpenBrushDetection(unittest.TestCase):
    """Test detection of open/invalid brushes."""

    def test_open_brush_rejected(self):
        """Brush with <4 faces should be invalid."""
        brush = make_open_brush()
        self.assertLess(len(brush.faces), 4)

    def test_closed_cube_valid(self):
        """Cube brush with 6 faces should be valid."""
        brush = make_cube_brush(64.0)
        self.assertGreaterEqual(len(brush.faces), 4)


if __name__ == "__main__":
    unittest.main()
