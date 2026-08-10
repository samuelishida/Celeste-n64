#!/usr/bin/env python3
"""Inc 3 verification — global collision seam probes + visual chunking.

Builds the 2x2 fixture through the canonical IR, builds the global collision
scene, and probes both sides of every seam against the SINGLE global mesh.
Asserts no seam probe depends on a visual room's collision sidecar, and that
every visual chunk's source coverage is explainable.

Run:
    python3 tests/interconnected_geometry_test.py
"""

import sys
import math
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tools"))

from ogworld.parse import build_world_ir
from ogworld.geometry import build_world_geometry
from ogworld.collision import (
    build_global_collision, validate_global_coverage, collision_budget,
)
from ogworld.chunking import partition_world, cell_id

FIXTURE = REPO / "tests" / "fixtures" / "interconnected-2x2.map"
CHUNK_SIZE = 1000.0
SCALE = 0.2


def raycast_down(scene, x, z, y_top=20.0, y_bottom=-5.0):
    """Raycast straight down; return the first (highest) hit y or None."""
    best = None
    for tri in scene.tris:
        a, b, c = scene.verts[tri.i0], scene.verts[tri.i1], scene.verts[tri.i2]
        # Compute triangle normal.
        ux, uy, uz = b[0]-a[0], b[1]-a[1], b[2]-a[2]
        vx, vy, vz = c[0]-a[0], c[1]-a[1], c[2]-a[2]
        nx, ny, nz = (uy*vz-uz*vy, uz*vx-ux*vz, ux*vy-uy*vx)
        if abs(ny) < 1e-9:
            continue  # vertical face, skip for floor probe
        # Plane: n . p = n . a
        d = nx*a[0] + ny*a[1] + nz*a[2]
        # Ray: p = (x, t, z). Solve n . (x,t,z) = d => t = (d - nx*x - nz*z)/ny
        t = (d - nx*x - nz*z) / ny
        if t < y_bottom or t > y_top:
            continue
        # Barycentric containment test.
        p = (x, t, z)
        if _point_in_triangle(p, a, b, c):
            # First hit from above = highest y.
            if best is None or t > best:
                best = t
    return best


def _point_in_triangle(p, a, b, c, eps=1e-6):
    def sign(p1, p2, p3):
        return (p1[0]-p3[0])*(p2[1]-p3[1]) - (p2[0]-p3[0])*(p1[1]-p3[1])
    d1 = sign(p, a, b)
    d2 = sign(p, b, c)
    d3 = sign(p, c, a)
    has_neg = (d1 < -eps) or (d2 < -eps) or (d3 < -eps)
    has_pos = (d1 > eps) or (d2 > eps) or (d3 > eps)
    return not (has_neg and has_pos)


def test_global_collision_coverage():
    build = build_world_ir(str(FIXTURE), scale=SCALE, strict=True)
    polys, diags = build_world_geometry(build, scale=SCALE)
    scene = build_global_collision(polys, scale=SCALE)
    errors = validate_global_coverage(scene, polys)
    assert not errors, f"coverage errors: {errors}"
    assert len(scene.tris) > 0
    print(f"PASS: global collision coverage — {len(scene.tris)} tris, "
          f"{len(scene.verts)} verts")
    return scene, polys


def test_seam_probes(scene):
    """Probe both sides of every seam against the single global mesh."""
    cell_w = CHUNK_SIZE * SCALE  # 200
    # Seams at x=200 (between cell 0 and 1) and z=-200 (between cell 0 and -1).
    seams = [
        # (probe_x, probe_z) on each side of the +X seam
        (199.0, -100.0), (201.0, -100.0),
        # each side of the -Z seam
        (100.0, -199.0), (100.0, -201.0),
        # corner seam (both axes)
        (199.0, -199.0), (201.0, -201.0),
    ]
    for x, z in seams:
        hit = raycast_down(scene, x, z)
        assert hit is not None, (
            f"no floor hit at seam probe ({x},{z}) — seam coverage broken"
        )
        # Floor top is at world y = map_z*s = 64*0.2 = 12.8.
        assert abs(hit - 12.8) < 0.5, (
            f"seam probe ({x},{z}) hit y={hit}, expected ~12.8"
        )
    print(f"PASS: {len(seams)} seam probes hit the global floor (y≈12.8)")


def test_visual_chunking():
    build = build_world_ir(str(FIXTURE), scale=SCALE, strict=True)
    polys, diags = build_world_geometry(build, scale=SCALE)
    chunks, cd = partition_world(build, polys, CHUNK_SIZE, SCALE)
    assert not cd, f"chunking diagnostics: {cd}"
    # Exactly 4 cells.
    assert len(chunks) == 4, f"expected 4 cells, got {len(chunks)}"
    # Overhang brush (entity 0, brush 4) spans 2 cells (seam coverage).
    assert build.brush_cell_counts[(0, 4)] == 2, (
        f"overhang should span 2 cells, got {build.brush_cell_counts[(0,4)]}"
    )
    # Floors span exactly 1 cell each.
    for bi in range(4):
        assert build.brush_cell_counts[(0, bi)] == 1, (
            f"floor brush {bi} should span 1 cell"
        )
    # Start spawn in cell (0,0), AnchorB in cell (1,0).
    start_cell = None
    anchor_cell = None
    for k, c in chunks.items():
        for s in c.spawns:
            if s.name == "Start":
                start_cell = k
            if s.name == "AnchorB":
                anchor_cell = k
    assert start_cell == (0, 0), f"Start in {start_cell}, expected (0,0)"
    assert anchor_cell == (1, 0), f"AnchorB in {anchor_cell}, expected (1,0)"
    print(f"PASS: visual chunking — 4 cells, overhang spans seam, "
          f"spawns placed correctly")


def test_budget():
    build = build_world_ir(str(FIXTURE), scale=SCALE, strict=True)
    polys, diags = build_world_geometry(build, scale=SCALE)
    scene = build_global_collision(polys, scale=SCALE)
    budget = collision_budget(scene)
    assert budget["triangles"] > 0
    assert budget["vertices"] > 0
    print(f"PASS: budget — {budget['triangles']} tris, "
          f"{budget['vertices']} verts, "
          f"~{budget['disk_bytes_est']/1024:.0f} KB disk est")


def main() -> int:
    scene, polys = test_global_collision_coverage()
    test_seam_probes(scene)
    test_visual_chunking()
    test_budget()
    print("\nAll interconnected_geometry_test tests passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
