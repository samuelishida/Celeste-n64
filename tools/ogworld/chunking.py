#!/usr/bin/env python3
"""Visual-room chunking (Inc 3).

Partitions visual geometry by world-XZ grid using the canonical transform and
assigns clipped visual polygons to every cell whose column intersects its
AABB, not only the center cell. Point entities are assigned to exactly one
cell. Collision geometry is NOT partitioned here (it is global).
"""

from __future__ import annotations

import math
from typing import List, Tuple, Dict, Optional

from .model import (
    WorldBuild, WorldPolygon, ChunkInput, SpawnRecord, CellKey,
)
from .class_policy import RENDER_STATIC, RENDER_ACTOR, RENDER_NONE

# Reuse the canonical cell_of from brush_grid (byte-for-byte the runtime).
from ogmap_lib.brush_grid import cell_of, cell_id
from ogmap_lib.brush_geom import clip_polygon_by_plane


def world_cell(point, chunk_size: float, scale: float) -> CellKey:
    """Map a world-space point to its world-XZ grid cell index.

    `point` is already in world space (Y-up). The grid is 2D in world XZ:
    ix = floor(world_x / (chunk_size*scale)), iz = floor(world_z / (chunk_size*scale)).
    """
    ix, iz = resolve_cell_index(point, chunk_size, scale)
    return (ix, iz)


def resolve_cell_index(world_pos, chunk_size: float, scale: float):
    """CANONICAL cell-resolution helper — mirrors C++
    `ResolveCellIndex` (render_origin_math.hpp) and the runtime
    `MapRuntime::ResolveCellByPosition`. Takes a world-space (Y-up) point and
    returns the (ix, iz) world-XZ grid indices.

    Grid is 2D in world XZ: ix = floor(world_x / (chunk_size*scale)),
    iz = floor(world_z / (chunk_size*scale)). world_z is depth (= -map_y),
    never map_z (the Quake UP axis).

    This is the single canonical Python implementation; `world_cell` and the
    brush-grid `cell_of` delegate to it so the formula cannot fork a 4th copy.
    Returns (None, None) if the cell size is non-positive (degenerate grid).
    """
    cell = chunk_size * scale
    if cell <= 0.0:
        return (None, None)
    return (
        int(math.floor(world_pos[0] / cell)),
        int(math.floor(world_pos[2] / cell)),
    )


def cells_intersecting_aabb(
    aabb_min, aabb_max, chunk_size: float, scale: float,
    eps: float = 1e-3,
) -> List[CellKey]:
    """Return every world-XZ cell whose column intersects the world AABB.

    `aabb_min`/`aabb_max` are world-space (Y-up) AABB corners. The grid is 2D
    in world XZ; the Y (up) extent is unbounded within a column.

    A stable epsilon policy: a brush whose AABB ends exactly on a seam
    (within `eps`) belongs only to the cell it occupies; a brush that crosses
    the seam (extends past it by more than `eps`) belongs to both sides. This
    prevents a floor that exactly spans one cell from spilling into the next.
    """
    cell = chunk_size * scale
    ix0 = int(math.floor((aabb_min[0] + eps) / cell))
    ix1 = int(math.floor((aabb_max[0] - eps) / cell))
    iz0 = int(math.floor((aabb_min[2] + eps) / cell))
    iz1 = int(math.floor((aabb_max[2] - eps) / cell))
    out: List[CellKey] = []
    for ix in range(ix0, ix1 + 1):
        for iz in range(iz0, iz1 + 1):
            out.append((ix, iz))
    return out


def _poly_aabb(poly: WorldPolygon) -> Tuple[Tuple[float, float, float], Tuple[float, float, float]]:
    xs = [v[0] for v in poly.verts]
    ys = [v[1] for v in poly.verts]
    zs = [v[2] for v in poly.verts]
    return (
        (min(xs), min(ys), min(zs)),
        (max(xs), max(ys), max(zs)),
    )


def clip_polygon_to_column(
    poly: WorldPolygon, cell: CellKey, chunk_size: float, scale: float
) -> Optional[WorldPolygon]:
    """Clip a world polygon to a cell's XZ column (Y unbounded).

    Returns a new WorldPolygon whose vertices lie within the cell's XZ
    footprint, or None if the polygon does not intersect the column. UVs are
    recomputed from the clipped Quake-space points (the polygon's source
    face is re-derived from the IR's source brushes).

    This bounds per-cell face/vertex counts for oversized brushes while
    preserving seam coverage.
    """
    cell_w = chunk_size * scale
    ix, iz = cell
    x0, x1 = ix * cell_w, (ix + 1) * cell_w
    z0, z1 = iz * cell_w, (iz + 1) * cell_w

    # Clip against the four vertical column planes (keeps dot(n,p)+dist>=0).
    verts = list(poly.verts)
    verts = clip_polygon_by_plane(verts, (1, 0, 0), -x0)   # x >= x0
    if len(verts) < 3:
        return None
    verts = clip_polygon_by_plane(verts, (-1, 0, 0), x1)   # x <= x1
    if len(verts) < 3:
        return None
    verts = clip_polygon_by_plane(verts, (0, 0, 1), -z0)   # z >= z0
    if len(verts) < 3:
        return None
    verts = clip_polygon_by_plane(verts, (0, 0, -1), z1)   # z <= z1
    if len(verts) < 3:
        return None

    # Recompute UVs from the source face (Quake-space points).
    sf = poly.src_face
    if sf is None:
        return None
    face = {
        "normal": _quake_normal(sf),
        "p1": sf.src_p1,
        "rotation": sf.rotation,
        "scale_u": sf.scale_u,
        "scale_v": sf.scale_v,
        "shift_u": sf.shift_u,
        "shift_v": sf.shift_v,
        "texture": sf.texture,
    }
    from ogmap_lib.texture_mapping import compute_uv
    inv_scale = 1.0 / scale
    uvs = []
    for v in verts:
        # world = (map_x*s, map_z*s, -map_y*s) => map = (wx/s, -wz/s, wy/s)
        q = (v[0] * inv_scale, -v[2] * inv_scale, v[1] * inv_scale)
        uvs.append(compute_uv(q, face, scale))

    return WorldPolygon(
        verts=tuple(verts),
        uvs=tuple(uvs),
        normal=poly.normal,
        material_id=poly.material_id,
        material_flags=poly.material_flags,
        collision_mode=poly.collision_mode,
        render_mode=poly.render_mode,
        entity_index=poly.entity_index,
        brush_index=poly.brush_index,
        face_index=poly.face_index,
        classname=poly.classname,
        texture=poly.texture,
        src_face=sf,
    )


def _quake_normal(sf) -> Tuple[float, float, float]:
    """Recover the Quake-space face normal from the transformed normal.

    transform_normal maps (x,y,z) -> (x,z,-y). Inverse: (x, -z, y).
    """
    nx, ny, nz = sf.normal
    return (nx, -nz, ny)


def partition_world(
    build: WorldBuild,
    polygons: List[WorldPolygon],
    chunk_size: float,
    scale: float,
    max_cells_per_brush: int = 64,
) -> Tuple[Dict[CellKey, ChunkInput], List[str]]:
    """Partition visual polygons + point spawns into visual cells.

    Returns (chunks, diagnostics). A visual polygon is assigned to every cell
    whose column intersects its AABB (seam coverage). Point spawns are assigned
    to exactly one cell (their origin cell).

    `max_cells_per_brush` guards against a pathological brush spanning too many
    cells; exceeding it produces a diagnostic (the caller may fail).
    """
    build.chunk_size = chunk_size
    chunks: Dict[CellKey, ChunkInput] = {}
    diagnostics: List[str] = []

    # Group polygons by source brush to enforce the per-brush cell cap.
    from collections import defaultdict
    by_brush: Dict[Tuple[int, int], List[WorldPolygon]] = defaultdict(list)
    for poly in polygons:
        if poly.render_mode != RENDER_STATIC:
            continue  # actor-owned geometry is not baked into room mesh
        by_brush[(poly.entity_index, poly.brush_index)].append(poly)

    for (ei, bi), polys in by_brush.items():
        # Compute the union AABB of the brush's visual polygons.
        if not polys:
            continue
        mn = (math.inf, math.inf, math.inf)
        mx = (-math.inf, -math.inf, -math.inf)
        for p in polys:
            pmin, pmax = _poly_aabb(p)
            mn = (min(mn[0], pmin[0]), min(mn[1], pmin[1]), min(mn[2], pmin[2]))
            mx = (max(mx[0], pmax[0]), max(mx[1], pmax[1]), max(mx[2], pmax[2]))
        cells = cells_intersecting_aabb(mn, mx, chunk_size, scale)
        if len(cells) > max_cells_per_brush:
            diagnostics.append(
                f"brush {ei}:{bi} spans {len(cells)} visual cells "
                f"(> {max_cells_per_brush}); consider a smaller chunk or "
                f"clipping"
            )
        build.brush_cell_counts[(ei, bi)] = len(cells)
        for cell in cells:
            chunks.setdefault(cell, ChunkInput(cell=cell))
            # Clip each polygon to the cell's column so oversized brushes do
            # not blow the per-cell face/vertex caps. A polygon that does not
            # intersect the column (after clipping) is dropped for that cell.
            clipped = []
            for p in polys:
                cp = clip_polygon_to_column(p, cell, chunk_size, scale)
                if cp is not None:
                    clipped.append(cp)
            existing = list(chunks[cell].polygons)
            existing.extend(clipped)
            chunks[cell] = ChunkInput(cell=cell, polygons=tuple(existing))

    # Assign point spawns to exactly one cell (their origin cell).
    for spawn in build.spawns:
        cell = world_cell(spawn.position, chunk_size, scale)
        chunks.setdefault(cell, ChunkInput(cell=cell))
        existing = list(chunks[cell].spawns)
        existing.append(spawn)
        chunks[cell] = ChunkInput(
            cell=cell,
            polygons=chunks[cell].polygons,
            spawns=tuple(existing),
        )

    return chunks, diagnostics


def build_adjacency(cells: Dict[CellKey, ChunkInput]) -> Dict[CellKey, Dict[str, CellKey]]:
    """Build the ±X/±Z adjacency graph for the visual cells."""
    adj: Dict[CellKey, Dict[str, CellKey]] = {}
    for cell in cells:
        ix, iz = cell
        adj[cell] = {
            "+X": (ix + 1, iz) if (ix + 1, iz) in cells else None,
            "-X": (ix - 1, iz) if (ix - 1, iz) in cells else None,
            "+Z": (ix, iz + 1) if (ix, iz + 1) in cells else None,
            "-Z": (ix, iz - 1) if (ix, iz - 1) in cells else None,
        }
    return adj
