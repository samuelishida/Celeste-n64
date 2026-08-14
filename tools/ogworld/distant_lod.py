#!/usr/bin/env python3
"""Offline distant-LOD generation (Inc 4).

Reads the baked per-cell visual geometry (the same canonical world polygons
that the per-room LVL writer consumes) and emits a coarse `distant.lvl`-style
mesh per cell. The coarse mesh keeps the world's silhouette while drastically
reducing face/vertex counts, so the distant pass can render many cells with
few triangles.

This is a mesh DECIMATION step, not a texture bake. It:
  - merges coplanar faces (within a normal/offset tolerance);
  - quantizes vertices to a coarse fixed-point grid at `kLodScale`;
  - emits one LVL2 file per cell (`<cell>_distant.lvl`) with flat per-material
    colors (texturing of distant cells is future work).

`kLodScale` must be chosen so that the full world extent in distant space stays
inside int16 (`distant_far / kLodScale <= 32767`). The plan pins `kLodScale`
at 0.25 (same as the runtime `lod_scale`), giving 4x int16 headroom.
"""

from __future__ import annotations

import math
import sys
from pathlib import Path
from typing import List, Tuple

sys.path.insert(0, str(Path(__file__).parent.parent))

from ogworld.model import WorldPolygon, ChunkInput
from lvl_format import LvlFile, Face as LvlFace, Vertex as LvlVertex
from writers.dlod_writer import write_dlod

# Fixed-point scale for distant coordinates (matches runtime `lod_scale`).
KLOD_SCALE = 0.25
# Coplanar merge tolerance: faces whose normals are within this angular cosine
# and whose plane offsets are within this world distance are merged.
MERGE_COS = 0.995
MERGE_OFFSET = 8.0
# Coarse grid quantization (world units) after merging.
QUANT = 16.0
# Default per-cell face budget (hard ceiling; the bake CLI can override).
DEFAULT_BUDGET = 20


def _face_plane(poly: WorldPolygon):
    """Return (nx, ny, nz, offset) for a polygon's plane."""
    n = poly.normal
    d = -(n[0] * poly.verts[0][0] + n[1] * poly.verts[0][1] +
          n[2] * poly.verts[0][2])
    return n, d


def _coplanar(n1, d1, n2, d2, m1, m2):
    """True if two planes are nearly the same (same normal dir, near offset)
    AND share the same material. A merged group must never span materials."""
    if m1 != m2:
        return False
    dot = n1[0] * n2[0] + n1[1] * n2[1] + n1[2] * n2[2]
    if dot < MERGE_COS:
        return False
    return abs(d1 - d2) < MERGE_OFFSET


# ── 2D plane helpers ──────────────────────────────────────────────────────

def _normalize(v):
    m = math.sqrt(v[0] ** 2 + v[1] ** 2 + v[2] ** 2)
    if m == 0.0:
        return (0.0, 0.0, 0.0)
    return (v[0] / m, v[1] / m, v[2] / m)


def _cross(a, b):
    return (a[1] * b[2] - a[2] * b[1],
            a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0])


def _dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def _plane_basis(n):
    """Two orthonormal in-plane vectors (u, v) for a plane normal n."""
    t = (1.0, 0.0, 0.0) if abs(n[0]) < 0.9 else (0.0, 1.0, 0.0)
    u = _normalize(_cross(n, t))
    v = _cross(n, u)
    return u, v


def _to2d(p, u, v, origin):
    # Project relative to `origin` (a point on the plane) so `_from2d` with the
    # same origin reconstructs the point exactly: p = origin + x·u + y·v.
    return (_dot((p[0] - origin[0], p[1] - origin[1], p[2] - origin[2]), u),
            _dot((p[0] - origin[0], p[1] - origin[1], p[2] - origin[2]), v))


def _from2d(x, y, u, v, origin):
    return (origin[0] + x * u[0] + y * v[0],
            origin[1] + x * u[1] + y * v[1],
            origin[2] + x * u[2] + y * v[2])


def _signed_area2d(pts):
    s = 0.0
    for i in range(len(pts)):
        x1, y1 = pts[i]
        x2, y2 = pts[(i + 1) % len(pts)]
        s += x1 * y2 - x2 * y1
    return s / 2.0


def _point_in_tri2d(p, a, b, c):
    def sign(p1, p2, p3):
        return ((p1[0] - p3[0]) * (p2[1] - p3[1]) -
                (p1[1] - p3[1]) * (p2[0] - p3[0]))
    d1 = sign(p, a, b)
    d2 = sign(p, b, c)
    d3 = sign(p, c, a)
    has_neg = (d1 < 0) or (d2 < 0) or (d3 < 0)
    has_pos = (d1 > 0) or (d2 > 0) or (d3 > 0)
    return not (has_neg and has_pos)


def _convex_hull2d(pts):
    """Andrew's monotone chain; returns the hull in CCW order."""
    pts = sorted(set(pts))
    if len(pts) < 3:
        return pts

    def cross(o, a, b):
        return ((a[0] - o[0]) * (b[1] - o[1]) -
                (a[1] - o[1]) * (b[0] - o[0]))

    lower = []
    for p in pts:
        while len(lower) >= 2 and cross(lower[-2], lower[-1], p) <= 0:
            lower.pop()
        lower.append(p)
    upper = []
    for p in reversed(pts):
        while len(upper) >= 2 and cross(upper[-2], upper[-1], p) <= 0:
            upper.pop()
        upper.append(p)
    return lower[:-1] + upper[:-1]


# ── Outline + fan ─────────────────────────────────────────────────────────

def _outline_of_group(group, quant: float = QUANT):
    """Trace the union outline of a coplanar group.

    Returns `(ring2d, (u, v, origin))` where `ring2d` is a CCW list of 2D
    points on the group's plane (quantized to `quant`), or None if the outline
    is non-simple (multiple loops / self-intersecting) — the caller falls
    back to a convex hull.
    """
    n = group[0].normal
    u, v = _plane_basis(n)
    origin = group[0].verts[0]

    def q2d(p):
        # Project relative to `origin` so `_from2d` reconstructs exactly.
        d = (p[0] - origin[0], p[1] - origin[1], p[2] - origin[2])
        return (round(_dot(d, u) / quant) * quant,
                round(_dot(d, v) / quant) * quant)

    # Collect undirected edges with occurrence counts. Boundary edges appear
    # exactly once; interior edges (shared by two same-side polygons) twice.
    edge_count = {}
    for poly in group:
        pts = [q2d(p) for p in poly.verts]
        for i in range(len(pts)):
            a = pts[i]
            b = pts[(i + 1) % len(pts)]
            if a == b:
                continue
            key = (a, b) if a < b else (b, a)
            edge_count[key] = edge_count.get(key, 0) + 1

    boundary = [k for k, c in edge_count.items() if c == 1]
    if not boundary:
        return None

    adj = {}
    for a, b in boundary:
        adj.setdefault(a, []).append(b)
        adj.setdefault(b, []).append(a)

    # Walk the boundary ring.
    start = boundary[0][0]
    ring = [start]
    used_edges = set()
    prev = None
    cur = start
    while True:
        nxts = [x for x in adj[cur] if x != prev]
        if not nxts:
            return None
        nxt = nxts[0]
        edge = (cur, nxt) if cur < nxt else (nxt, cur)
        if edge in used_edges:
            return None
        used_edges.add(edge)
        if nxt == start and len(ring) > 2:
            break
        ring.append(nxt)
        prev, cur = cur, nxt
        if len(ring) > len(boundary) + 1:
            return None

    ring = ring[:-1]
    if len(ring) < 3:
        return None
    # Simple ring: every vertex has degree 2 and every boundary edge is used
    # exactly once (a disconnected second loop would leave edges unused).
    for p in ring:
        if len(adj.get(p, [])) != 2:
            return None
    if len(used_edges) != len(boundary):
        return None

    if _signed_area2d(ring) < 0:
        ring.reverse()
    return ring, (u, v, origin)


def _fan_polygon(ring2d):
    """Ear-clip a CCW 2D ring into triangles.

    Returns a list of `(i0, i1, i2)` index triples into `ring2d`, or None if
    the ring is non-simple (caller falls back to a convex hull).
    """
    pts = list(ring2d)
    n = len(pts)
    if n < 3:
        return None
    if _signed_area2d(pts) < 0:
        pts.reverse()

    indices = list(range(n))
    tris = []
    guard = 0
    while len(indices) > 3:
        guard += 1
        if guard > n * n:
            return None
        ear = None
        for i in range(len(indices)):
            i0 = indices[i - 1]
            i1 = indices[i]
            i2 = indices[(i + 1) % len(indices)]
            a = pts[i0]
            b = pts[i1]
            c = pts[i2]
            cross = ((b[0] - a[0]) * (c[1] - a[1]) -
                     (b[1] - a[1]) * (c[0] - a[0]))
            if cross <= 0:
                continue  # reflex or degenerate
            inside = False
            for j in indices:
                if j in (i0, i1, i2):
                    continue
                if _point_in_tri2d(pts[j], a, b, c):
                    inside = True
                    break
            if inside:
                continue
            ear = (i0, i1, i2)
            break
        if ear is None:
            return None
        tris.append(ear)
        indices.pop(indices.index(ear[1]))
    tris.append((indices[0], indices[1], indices[2]))
    return tris


def _group_triangles(group):
    """Fan a coplanar group into 3D triangles.

    Returns a list of `(tri3d, material_id)` where `tri3d` is a 3-tuple of
    3D points. Uses the union outline + ear-clipping, with a convex-hull
    fallback for non-simple outlines.
    """
    mat = group[0].material_id
    n = group[0].normal
    u, v = _plane_basis(n)
    origin = group[0].verts[0]

    outline = _outline_of_group(group)
    if outline is None:
        # Convex-hull fallback over all the group's vertices.
        pts2d = [_to2d(p, u, v, origin) for poly in group for p in poly.verts]
        hull = _convex_hull2d(pts2d)
        if len(hull) < 3:
            return []
        tris = []
        for i in range(1, len(hull) - 1):
            tris.append((_from2d(hull[0][0], hull[0][1], u, v, origin),
                         _from2d(hull[i][0], hull[i][1], u, v, origin),
                         _from2d(hull[i + 1][0], hull[i + 1][1], u, v, origin)))
        return [(t, mat) for t in tris]

    ring2d, (u2, v2, origin2) = outline
    tris2d = _fan_polygon(ring2d)
    if tris2d is None:
        hull = _convex_hull2d(ring2d)
        if len(hull) < 3:
            return []
        tris = []
        for i in range(1, len(hull) - 1):
            tris.append((_from2d(hull[0][0], hull[0][1], u2, v2, origin2),
                         _from2d(hull[i][0], hull[i][1], u2, v2, origin2),
                         _from2d(hull[i + 1][0], hull[i + 1][1], u2, v2, origin2)))
        return [(t, mat) for t in tris]

    tris = []
    for (i0, i1, i2) in tris2d:
        tris.append((_from2d(ring2d[i0][0], ring2d[i0][1], u2, v2, origin2),
                     _from2d(ring2d[i1][0], ring2d[i1][1], u2, v2, origin2),
                     _from2d(ring2d[i2][0], ring2d[i2][1], u2, v2, origin2)))
    return [(t, mat) for t in tris]


def _group_coplanar(polygons: List[WorldPolygon]):
    """Group coplanar polygons by (plane, material)."""
    groups = []
    used = [False] * len(polygons)
    for i, p in enumerate(polygons):
        if used[i]:
            continue
        n1, d1 = _face_plane(p)
        m1 = p.material_id
        group = [p]
        used[i] = True
        for j in range(i + 1, len(polygons)):
            if used[j]:
                continue
            n2, d2 = _face_plane(polygons[j])
            if _coplanar(n1, d1, n2, d2, m1, polygons[j].material_id):
                group.append(polygons[j])
                used[j] = True
        groups.append(group)
    return groups


# ── Quantize + decimate ───────────────────────────────────────────────────

def _tri_area(a, b, c):
    ab = (b[0] - a[0], b[1] - a[1], b[2] - a[2])
    ac = (c[0] - a[0], c[1] - a[1], c[2] - a[2])
    cr = _cross(ab, ac)
    return 0.5 * math.sqrt(cr[0] ** 2 + cr[1] ** 2 + cr[2] ** 2)


def _quantize(triangles):
    """Dedupe triangle vertices onto the QUANT grid.

    Returns `(verts, faces, face_groups)` where `verts` is a list of world
    points, `faces` is a list of `(idx_tuple, material_id)`, and `face_groups`
    is a parallel list of group ids (for the over-budget fallback).
    """
    vert_map = {}
    verts = []
    faces = []
    face_groups = []
    for gi, (tri_pts, mat) in enumerate(triangles):
        idxs = []
        for p in tri_pts:
            k = (round(p[0] / QUANT) * QUANT,
                 round(p[1] / QUANT) * QUANT,
                 round(p[2] / QUANT) * QUANT)
            if k not in vert_map:
                vert_map[k] = len(verts)
                verts.append(k)
            idxs.append(vert_map[k])
        if len(set(idxs)) == 3:
            faces.append((tuple(idxs), mat))
            face_groups.append(gi)
    return verts, faces, face_groups


def _cluster(verts, faces, face_groups, grid):
    """Vertex-cluster on a `grid`-sized lattice; drop degenerate triangles."""
    rep = {}
    for i, p in enumerate(verts):
        key = (round(p[0] / grid) * grid,
               round(p[1] / grid) * grid,
               round(p[2] / grid) * grid)
        if key not in rep:
            rep[key] = i
    # Per-vertex representative (the first vertex in each cluster).
    cluster_of = [rep[(round(p[0] / grid) * grid,
                       round(p[1] / grid) * grid,
                       round(p[2] / grid) * grid)] for p in verts]

    new_verts = []
    vmap = {}
    new_faces = []
    new_fg = []
    for (idx_tuple, mat), gi in zip(faces, face_groups):
        new_idx = []
        for vi in idx_tuple:
            r = cluster_of[vi]
            if r not in vmap:
                vmap[r] = len(new_verts)
                new_verts.append(verts[r])
            new_idx.append(vmap[r])
        if len(set(new_idx)) == 3:
            a = new_verts[new_idx[0]]
            b = new_verts[new_idx[1]]
            c = new_verts[new_idx[2]]
            if _tri_area(a, b, c) > 1e-6:
                new_faces.append((tuple(new_idx), mat))
                new_fg.append(gi)
    return new_verts, new_faces, new_fg


def _drop_smallest_groups(verts, faces, face_groups, budget):
    """Over-budget fallback: drop the smallest-area coplanar groups until the
    cell meets the budget. Always keeps at least one face so the cell stays
    visible. Returns `(verts, faces, budget_met)`."""
    from collections import defaultdict
    group_faces = defaultdict(list)
    group_area = defaultdict(float)
    for (idx_tuple, mat), gi in zip(faces, face_groups):
        group_faces[gi].append((idx_tuple, mat))
        a = verts[idx_tuple[0]]
        b = verts[idx_tuple[1]]
        c = verts[idx_tuple[2]]
        group_area[gi] += _tri_area(a, b, c)

    sorted_groups = sorted(group_faces.keys(), key=lambda g: group_area[g])
    kept = set()
    count = 0
    for g in sorted_groups:
        if count == 0 or count + len(group_faces[g]) <= budget:
            kept.add(g)
            count += len(group_faces[g])
        else:
            break

    new_verts = []
    vmap = {}
    new_faces = []
    for g in sorted(kept):
        for (idx_tuple, mat) in group_faces[g]:
            new_idx = []
            for vi in idx_tuple:
                if vi not in vmap:
                    vmap[vi] = len(new_verts)
                    new_verts.append(verts[vi])
                new_idx.append(vmap[vi])
            new_faces.append((tuple(new_idx), mat))
    return new_verts, new_faces, count <= budget


def _decimate_to_budget(verts, faces, face_groups, budget):
    """Reduce a cell to `budget` faces via vertex clustering on a coarsening
    grid, then (if still over) by dropping the smallest coplanar groups.

    Returns `(verts, faces, budget_met)`.
    """
    if len(faces) <= budget:
        return verts, faces, True
    for grid in (QUANT, QUANT * 2, QUANT * 3):
        v, f, fg = _cluster(verts, faces, face_groups, grid)
        if len(f) <= budget:
            return v, f, True
    return _drop_smallest_groups(verts, faces, face_groups, budget)


def build_distant_lod(polygons: List[WorldPolygon],
                      lod_scale: float = KLOD_SCALE,
                      budget: int = DEFAULT_BUDGET):
    """Decimate a cell's polygons into a coarse mesh.

    Returns `(verts, faces, budget_met)` where `verts` is a list of (x, y, z)
    world-space points, `faces` is a list of `(vertex_index_tuple,
    material_id)` (triangles), and `budget_met` is True if the cell met the
    per-cell face budget.
    """
    groups = _group_coplanar(polygons)
    triangles = []
    for group in groups:
        triangles.extend(_group_triangles(group))
    verts, faces, face_groups = _quantize(triangles)
    verts, faces, budget_met = _decimate_to_budget(verts, faces, face_groups,
                                                   budget)
    return verts, faces, budget_met


def build_distant_dlod(chunk: ChunkInput, material_count: int, origin,
                       out_path: str, lod_scale: float = KLOD_SCALE,
                       budget: int = DEFAULT_BUDGET) -> Optional[dict]:
    """High-level entry: decimate + emit a compact `.dlod` for one cell.

    Emits a single-direction DLOD v2 (direction_count=1, flags bit0 clear).
    Returns a stats dict (face/vert counts + `budget_met`), or None if the
    cell has no renderable geometry.
    """
    if not chunk.polygons:
        return None
    verts, faces, budget_met = build_distant_lod(list(chunk.polygons),
                                                 lod_scale, budget)
    write_dlod([(verts, faces)], material_count, origin, out_path,
               scale=lod_scale, per_direction=False)
    return {"faces": len(faces), "vertices": len(verts),
            "budget_met": budget_met}


# ── Same-geometry, painter-sorted direction variants (Inc 1) ──────────────

# Direction index → outward normal (matches lod_math.hpp DirectionalIndexFromDelta):
#   0 = +Z (south), 1 = -Z (north), 2 = +X (east), 3 = -X (west)
_DIRECTION_NORMALS = [
    (0.0, 0.0, 1.0),
    (0.0, 0.0, -1.0),
    (1.0, 0.0, 0.0),
    (-1.0, 0.0, 0.0),
]
# Per-direction face budget (Inc 1). All 4 directions share ONE decimated
# mesh, so this is the single per-cell budget (matches the bake CLI default).
DEFAULT_DIRECTION_BUDGET = 20


def _sort_direction(faces, verts, dir_index):
    """Reorder `faces` back-to-front along `dir_index`'s axis.

    Stable sort by `(material_id, dot(centroid, dir_normal))` ascending so
    material runs still form AND within-material painter order survives (the
    runtime's `SortFacesByMaterial` is a stable sort). All 4 directions
    reference the SAME triangle set — only the order differs, so switching
    directions never swaps geometry (no pop, no holes).
    """
    n = _DIRECTION_NORMALS[dir_index]

    def key(face):
        idx_tuple, mat = face
        cx = sum(verts[i][0] for i in idx_tuple) / 3.0
        cy = sum(verts[i][1] for i in idx_tuple) / 3.0
        cz = sum(verts[i][2] for i in idx_tuple) / 3.0
        return (mat, cx * n[0] + cy * n[1] + cz * n[2])

    return sorted(faces, key=key)


def build_distant_dlod_directional(chunk: ChunkInput, material_count: int,
                                  origin, out_path: str,
                                  lod_scale: float = KLOD_SCALE,
                                  budget: int = DEFAULT_BUDGET,
                                  dir_budget: Optional[int] = None
                                  ) -> Optional[dict]:
    """High-level entry: emit a 4-direction DLOD v2 for one cell.

    Decimates the cell ONCE to a single 360° mesh (budget = `dir_budget`,
    defaulting to `budget`), then emits 4 direction sections that reference
    the SAME triangle set, reordered back-to-front along each direction axis
    (painter's algorithm, Z off — no geometry swap, no pop). Returns a stats
    dict (face/vert counts + `budget_met`), or None if the cell has no
    renderable geometry.
    """
    if dir_budget is None:
        dir_budget = budget
    if not chunk.polygons:
        return None
    verts, faces, budget_met = build_distant_lod(list(chunk.polygons),
                                                 lod_scale, dir_budget)
    directions = [(verts, _sort_direction(faces, verts, d))
                  for d in range(4)]
    write_dlod(directions, material_count, origin, out_path,
               scale=lod_scale, per_direction=True)
    # `faces` is the shared decimated mesh (1×); the DLOD file carries 4
    # direction copies, so report the file's total face count (matches the
    # DLOD header `face_count` and the pre-Inc-1 semantics).
    return {"faces": len(faces) * len(directions),
            "vertices": sum(len(v) for v, _ in directions),
            "budget_met": budget_met}
