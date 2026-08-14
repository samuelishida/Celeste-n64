#!/usr/bin/env python3
"""Inc 2 distant-decimation contract (host-side).

Asserts the new decimator in `tools/ogworld/distant_lod.py`:
  - outline: two coplanar quads forming an L → 1 polygon whose AABB covers the
    union AABB;
  - material split: two coplanar faces with different `material_id` stay
    separate polygons with correct ids;
  - budget: a synthetic cell of N=80 faces decimates to ≤ budget with zero
    degenerate triangles;
  - winding: every emitted triangle is CCW in the group's plane frame;
  - coverage: decimated AABB covers ≥ 90% of the source AABB per axis
    (coverage wins over the zero-degenerate rule if they conflict);
  - quantization: all verts on the QUANT grid.

Run:
    python3 tests/distant_decimation_contract.py
"""

import sys
import math
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tools"))

from ogworld.distant_lod import (
    build_distant_lod, _group_coplanar, _group_triangles, _quantize,
    _outline_of_group, _fan_polygon, _signed_area2d, _convex_hull2d,
    _direction_silhouette, _DIRECTION_NORMALS,
    QUANT, DEFAULT_BUDGET, DEFAULT_DIRECTION_BUDGET,
)
from ogworld.model import WorldPolygon


def _poly(verts, mat=0, normal=None):
    """Build a WorldPolygon with a given vertex ring (CCW around normal)."""
    if normal is None:
        # Compute a normal from the first three verts.
        a, b, c = verts[0], verts[1], verts[2]
        ab = (b[0] - a[0], b[1] - a[1], b[2] - a[2])
        ac = (c[0] - a[0], c[1] - a[1], c[2] - a[2])
        n = (ab[1] * ac[2] - ab[2] * ac[1],
             ab[2] * ac[0] - ab[0] * ac[2],
             ab[0] * ac[1] - ab[1] * ac[0])
        m = math.sqrt(n[0] ** 2 + n[1] ** 2 + n[2] ** 2)
        normal = (n[0] / m, n[1] / m, n[2] / m)
    return WorldPolygon(
        verts=tuple(verts),
        uvs=tuple((0.0, 0.0) for _ in verts),
        normal=normal,
        material_id=mat,
        material_flags=0,
        collision_mode="solid",
        render_mode="static",
        entity_index=0, brush_index=0, face_index=0,
        classname="", texture="",
    )


def _aabb(pts):
    xs = [p[0] for p in pts]
    ys = [p[1] for p in pts]
    zs = [p[2] for p in pts]
    return ((min(xs), min(ys), min(zs)), (max(xs), max(ys), max(zs)))


def _aabb_cover(src_pts, dst_pts):
    """Per-axis coverage of dst AABB over src AABB (0..1)."""
    smin, smax = _aabb(src_pts)
    dmin, dmax = _aabb(dst_pts)
    ratios = []
    for i in range(3):
        s = smax[i] - smin[i]
        if s <= 1e-9:
            continue
        lo = max(smin[i], dmin[i])
        hi = min(smax[i], dmax[i])
        inter = max(0.0, hi - lo)
        ratios.append(inter / s)
    return min(ratios) if ratios else 1.0


def _tri_verts(verts, idx_tuple):
    return [verts[i] for i in idx_tuple]


def test_outline_union():
    """Two coplanar quads forming an L merge into one polygon covering the
    union AABB."""
    # Two quads on the z=0 plane, sharing an edge, forming an L.
    q1 = [(0, 0, 0), (10, 0, 0), (10, 0, 10), (0, 0, 10)]
    q2 = [(10, 0, 0), (20, 0, 0), (20, 0, 10), (10, 0, 10)]
    polys = [_poly(q1, mat=0), _poly(q2, mat=0)]
    groups = _group_coplanar(polys)
    assert len(groups) == 1, f"expected 1 coplanar group, got {len(groups)}"
    tris = _group_triangles(groups[0])
    assert len(tris) >= 1, "expected at least one triangle"
    src_pts = [p for poly in polys for p in poly.verts]
    dst_pts = [p for t, _ in tris for p in t]
    cover = _aabb_cover(src_pts, dst_pts)
    assert cover >= 0.9, f"outline union AABB coverage {cover:.2f} < 0.9"
    print(f"PASS: outline union -> {len(tris)} tris, coverage {cover:.2f}")


def test_material_split():
    """Two coplanar faces with different material_id stay separate polygons
    with correct ids."""
    q1 = [(0, 0, 0), (10, 0, 0), (10, 0, 10), (0, 0, 10)]
    q2 = [(10, 0, 0), (20, 0, 0), (20, 0, 10), (10, 0, 10)]
    polys = [_poly(q1, mat=0), _poly(q2, mat=1)]
    groups = _group_coplanar(polys)
    assert len(groups) == 2, f"expected 2 groups (material split), got {len(groups)}"
    mats = sorted({g[0].material_id for g in groups})
    assert mats == [0, 1], f"material ids not preserved: {mats}"
    print("PASS: material split -> 2 groups with ids [0, 1]")


def test_budget_and_no_degenerate():
    """A synthetic cell of N=80 faces decimates to ≤ budget with zero
    degenerate triangles."""
    polys = []
    # 80 distinct small quads scattered on the z=0 plane (different planes so
    # they don't all merge into one group).
    for i in range(80):
        x0 = (i % 10) * 12.0
        y0 = (i // 10) * 12.0
        q = [(x0, y0, 0), (x0 + 8, y0, 0), (x0 + 8, y0 + 8, 0), (x0, y0 + 8, 0)]
        polys.append(_poly(q, mat=i % 3))
    verts, faces, budget_met = build_distant_lod(polys, budget=DEFAULT_BUDGET)
    assert budget_met, "cell should meet the budget"
    assert len(faces) <= DEFAULT_BUDGET, (
        f"faces {len(faces)} > budget {DEFAULT_BUDGET}")
    # Zero degenerate triangles: every face has 3 distinct verts with area.
    for idx_tuple, _ in faces:
        assert len(set(idx_tuple)) == 3, f"degenerate face {idx_tuple}"
        a, b, c = _tri_verts(verts, idx_tuple)
        ab = (b[0] - a[0], b[1] - a[1], b[2] - a[2])
        ac = (c[0] - a[0], c[1] - a[1], c[2] - a[2])
        cr = (ab[1] * ac[2] - ab[2] * ac[1],
              ab[2] * ac[0] - ab[0] * ac[2],
              ab[0] * ac[1] - ab[1] * ac[0])
        area = 0.5 * math.sqrt(cr[0] ** 2 + cr[1] ** 2 + cr[2] ** 2)
        assert area > 1e-6, f"zero-area triangle {idx_tuple}"
    print(f"PASS: budget -> {len(faces)} faces (<= {DEFAULT_BUDGET}), "
          f"zero degenerate")


def test_winding_ccw():
    """Every emitted triangle is CCW in its group's plane frame."""
    # A concave L-shaped polygon on the z=0 plane.
    lshape = [(0, 0, 0), (20, 0, 0), (20, 10, 0), (10, 10, 0),
              (10, 20, 0), (0, 20, 0)]
    poly = _poly(lshape, mat=0)
    groups = _group_coplanar([poly])
    tris = _group_triangles(groups[0])
    assert len(tris) >= 1, "expected triangles from L-shape"
    n = poly.normal
    u, v = _plane_basis(n)
    for t, _ in tris:
        pts2d = [(_dot(p, u), _dot(p, v)) for p in t]
        area = _signed_area2d(pts2d)
        assert area > 0, f"triangle not CCW: {pts2d} area {area}"
    print(f"PASS: winding -> {len(tris)} CCW triangles")


def test_quantization():
    """All emitted verts lie on the QUANT grid."""
    polys = []
    for i in range(20):
        x0 = (i % 5) * 12.0
        y0 = (i // 5) * 12.0
        q = [(x0, y0, 0), (x0 + 8, y0, 0), (x0 + 8, y0 + 8, 0), (x0, y0 + 8, 0)]
        polys.append(_poly(q, mat=0))
    verts, faces, _ = build_distant_lod(polys, budget=DEFAULT_BUDGET)
    for p in verts:
        for c in p:
            assert abs(c / QUANT - round(c / QUANT)) < 1e-6, (
                f"vertex {p} not on QUANT grid")
    print(f"PASS: quantization -> {len(verts)} verts on QUANT grid")


def test_direction_silhouette_coverage():
    """Each direction's mesh AABB covers ≥ 90% of the cell's projection from
    that direction."""
    # A proper axis-aligned box (6 faces), so each direction sees one big wall.
    # Sized 200 units so QUANT=16 quantization doesn't collapse the extent
    # (real cells are 240 units).
    S = 200.0
    polys = []
    box = [
        # +Z wall (z=S), normal +Z
        [(0, 0, S), (S, 0, S), (S, S, S), (0, S, S)],
        # -Z wall (z=0), normal -Z (reversed winding)
        [(0, S, 0), (S, S, 0), (S, 0, 0), (0, 0, 0)],
        # +X wall (x=S), normal +X
        [(S, 0, 0), (S, 0, S), (S, S, S), (S, S, 0)],
        # -X wall (x=0), normal -X (reversed winding)
        [(0, S, 0), (0, S, S), (0, 0, S), (0, 0, 0)],
        # top (y=S), normal +Y
        [(0, S, 0), (S, S, 0), (S, S, S), (0, S, S)],
        # bottom (y=0), normal -Y (reversed winding)
        [(0, 0, 0), (S, 0, 0), (S, 0, S), (0, 0, S)],
    ]
    for q in box:
        polys.append(_poly(q, mat=0))

    for d in range(4):
        verts, faces, met = _direction_silhouette(polys, d,
                                                  DEFAULT_DIRECTION_BUDGET)
        assert len(faces) > 0, f"direction {d} should have geometry"
        assert len(faces) <= DEFAULT_DIRECTION_BUDGET, (
            f"direction {d} faces {len(faces)} > budget")
        # Coverage: the direction's mesh AABB covers ≥ 90% of the cell's
        # projection from that direction (the facing wall's extent).
        n = _DIRECTION_NORMALS[d]
        src_pts = []
        for p in polys:
            if _dot(p.normal, n) >= 0.7071:
                for v in p.verts:
                    src_pts.append(_project(v, n))
        dst_pts = []
        for idx_tuple, _ in faces:
            for vi in idx_tuple:
                dst_pts.append(_project(verts[vi], n))
        cover = _aabb_cover2d(src_pts, dst_pts)
        assert cover >= 0.9, (
            f"direction {d} coverage {cover:.2f} < 0.9")
    print("PASS: direction silhouette coverage (4 dirs, each ≤ budget)")


def _aabb_cover2d(src_pts, dst_pts):
    """Per-axis coverage of dst 2D AABB over src 2D AABB (0..1)."""
    sx = [p[0] for p in src_pts]
    sy = [p[1] for p in src_pts]
    dx = [p[0] for p in dst_pts]
    dy = [p[1] for p in dst_pts]
    ratios = []
    for (smin, smax, dmin, dmax) in ((min(sx), max(sx), min(dx), max(dx)),
                                     (min(sy), max(sy), min(dy), max(dy))):
        s = smax - smin
        if s <= 1e-9:
            continue
        lo = max(smin, dmin)
        hi = min(smax, dmax)
        inter = max(0.0, hi - lo)
        ratios.append(inter / s)
    return min(ratios) if ratios else 1.0


def _project(p, n):
    """Project a 3D point onto the plane perpendicular to n (2 axes)."""
    # Pick two orthonormal in-plane axes.
    t = (1.0, 0.0, 0.0) if abs(n[0]) < 0.9 else (0.0, 1.0, 0.0)
    u = _cross3(n, t)
    v = _cross3(n, u)
    return (_dot(p, u), _dot(p, v))


def _cross3(a, b):
    return (a[1] * b[2] - a[2] * b[1],
            a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0])


def _dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def _plane_basis(n):
    t = (1.0, 0.0, 0.0) if abs(n[0]) < 0.9 else (0.0, 1.0, 0.0)
    m = math.sqrt(t[0] ** 2 + t[1] ** 2 + t[2] ** 2)
    t = (t[0] / m, t[1] / m, t[2] / m)
    u = (n[1] * t[2] - n[2] * t[1],
         n[2] * t[0] - n[0] * t[2],
         n[0] * t[1] - n[1] * t[0])
    mu = math.sqrt(u[0] ** 2 + u[1] ** 2 + u[2] ** 2)
    u = (u[0] / mu, u[1] / mu, u[2] / mu)
    v = (n[1] * u[2] - n[2] * u[1],
         n[2] * u[0] - n[0] * u[2],
         n[0] * u[1] - n[1] * u[0])
    return u, v


if __name__ == "__main__":
    test_outline_union()
    test_material_split()
    test_budget_and_no_degenerate()
    test_winding_ccw()
    test_quantization()
    test_direction_silhouette_coverage()
    print("ALL PASS")
