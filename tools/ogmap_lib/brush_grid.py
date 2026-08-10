#!/usr/bin/env python3
"""Grid partitioning for OG maps.

Assigns each brush-bearing entity's brushes to a 2D grid cell by the brush's
world AABB center, mirroring the OG's GridPartition<Solid> approach
(Map.cs:213-219) but as a deliberate 2D-XZ deviation (vertical columns, not
the OG's 3D grid). Each brush is assigned to exactly one cell by its bounds
center, so a brush is never clipped across cells — no seam holes.

Point entities (PlayerSpawn, Strawberry, Cassette, Refill, Spring, Node, ...)
are assigned to the cell of their `origin`.

AXIS CONVENTION: the grid is 2D in WORLD XZ. Cell index ix =
floor(world_x / (chunk_size*scale)) and iz = floor(world_z /
(chunk_size*scale)) where world = transform_point(map) = (map_x*s, map_z*s,
-map_y*s). The second grid axis is world Z (depth, = -map_y) — NOT map_z,
which is the Quake UP axis. `cell_of` performs this world-space transform
itself, using byte-for-byte the same arithmetic as the runtime
Map::ResolveCellByPosition, so bake and runtime can never disagree on a cell
— including exactly at seams.
"""

from __future__ import annotations

import math
from typing import List, Tuple, Dict, Optional

# Import only from sibling submodules (already loaded) to avoid a circular
# import with the parent __init__.py, which imports this module at the end.
from .brush_geom import compute_face_polygon
from . import Vec3, vadd, vsub, vscale, vdot, vcross, vlength, vnormalize

# Type-only imports from the parent package (resolved at type-check time;
# not imported at runtime to avoid the cycle).
from typing import TYPE_CHECKING
if TYPE_CHECKING:
    from . import ParsedMap, Entity, Brush
    from . import classify_entity, is_skipped


CellKey = Tuple[int, int]  # (ix, iz) — 2D XZ grid index


def compute_brush_aabb(brush: Brush) -> Tuple[Vec3, Vec3]:
    """Compute the axis-aligned bounding box of a brush in MAP (Quake) units.

    Derives vertices from each face polygon via compute_face_polygon, then
    takes the min/max across all face vertices. Returns ((minx,miny,minz),
    (maxx,maxy,maxz)). For a brush with no computable geometry, returns a
    zero-size box at the origin.
    """
    all_pts: List[Vec3] = []
    for i in range(len(brush.faces)):
        poly = compute_face_polygon(brush.faces, i)
        all_pts.extend(poly)
    if not all_pts:
        return ((0.0, 0.0, 0.0), (0.0, 0.0, 0.0))
    xs = [p[0] for p in all_pts]
    ys = [p[1] for p in all_pts]
    zs = [p[2] for p in all_pts]
    return (
        (min(xs), min(ys), min(zs)),
        (max(xs), max(ys), max(zs)),
    )


def brush_center(brush: Brush) -> Vec3:
    """World (map-unit) center of a brush's AABB."""
    (mn, mx) = compute_brush_aabb(brush)
    return (
        (mn[0] + mx[0]) * 0.5,
        (mn[1] + mx[1]) * 0.5,
        (mn[2] + mx[2]) * 0.5,
    )


def cell_of(point: Vec3, chunk_size: float, scale: float) -> CellKey:
    """Map a MAP-unit point to its world-space XZ grid cell index.

    Transforms the map point to world coords (transform_point: world =
    (x*s, z*s, -y*s)) then floors by (chunk_size*scale) — byte-for-byte the
    same formula the runtime Map::ResolveCellByPosition uses
    (floor(world / (chunk_size * scale))). The second axis is world Z
    (= -map_y, depth) — NOT map_z, the Quake UP axis. Do NOT use a map-unit
    shortcut like floor(-map_y/cs): scale=0.2 is not binary-exact, and the
    two expressions can differ by one cell exactly at a seam.
    """
    cell = chunk_size * scale
    wx = point[0] * scale
    wz = -point[1] * scale
    return (
        int(math.floor(wx / cell)),
        int(math.floor(wz / cell)),
    )


def partition_parsed_map(
    parsed_map: ParsedMap,
    chunk_size: float,
    scale: float,
) -> Dict[CellKey, List[int]]:
    """Partition a ParsedMap's entities into grid cells.

    Returns a dict mapping cell_key -> list of entity indices into
    parsed_map.entities that belong to that cell.

    Assignment rules:
    - Brush-bearing entities: each BRUSH is assigned to the cell of its own
      AABB center (worldspawn brushes are distributed per-brush, not by the
      entity). An entity appears in every cell that owns at least one of its
      brushes, with a per-cell sub-entity carrying only those brushes.
    - Point entities (no brushes): assigned to the cell of their `origin`.

    Entities that are skipped (is_skipped) are still partitioned so their
    brushes (if any) are not lost, but skipped entities typically have no
    brushes. Unknown classes with brushes are partitioned too — the writers
    will drop them via classify_entity() == None as today.

    Empty cells are not emitted.
    """
    cells: Dict[CellKey, List[int]] = {}
    for ei, ent in enumerate(parsed_map.entities):
        if ent.brushes:
            # Distribute per-brush by brush-center. An entity index may appear
            # in multiple cells; bake_map_pack builds a per-cell sub-entity
            # carrying only that cell's brushes.
            for brush in ent.brushes:
                c = brush_center(brush)
                key = cell_of(c, chunk_size, scale)
                cells.setdefault(key, [])
                if ei not in cells[key]:
                    cells[key].append(ei)
        else:
            # Point entity: assign by origin.
            key = cell_of(ent.origin, chunk_size, scale)
            cells.setdefault(key, []).append(ei)
    return cells


def entity_brushes_in_cell(
    entity: Entity,
    cell_key: CellKey,
    chunk_size: float,
    scale: float,
) -> List[Brush]:
    """Return the subset of an entity's brushes whose AABB center is in cell_key."""
    out: List[Brush] = []
    for brush in entity.brushes:
        if cell_of(brush_center(brush), chunk_size, scale) == cell_key:
            out.append(brush)
    return out


def world_aabb_for_cell(
    parsed_map: ParsedMap,
    cell_key: CellKey,
    chunk_size: float,
    scale: float,
) -> Tuple[Tuple[float, float, float], Tuple[float, float, float]]:
    """Compute the world-space AABB of all geometry assigned to a cell.

    Used for the map-pack manifest's per-room AABB (preload culling only —
    NOT for active-cell resolution, which uses the grid index directly).
    Falls back to the cell's own grid-aligned AABB if no brush geometry is
    computable.

    The second grid axis is world Z (depth = -map_y), matching cell_of and
    the runtime. The returned AABB is in WORLD space (post-scale, Y-up):
    map_z (up) maps to world Y, map_y maps to world Z (a flipped axis, so
    the Z min/max are swapped vs the raw map extents).
    """
    ix, iz = cell_key
    # Grid-aligned fallback AABB. In MAP units the cell spans map_x in
    # [ix*cs, (ix+1)*cs] and map_y (depth) in [iz*cs, (iz+1)*cs]; map_z (up)
    # is unbounded within a column, so use a generous default. Converted to
    # world space below (map_y -> world Z is flipped, hence the min/max swap).
    fb_min_map = (ix * chunk_size, iz * chunk_size, -8192.0)
    fb_max_map = ((ix + 1) * chunk_size, (iz + 1) * chunk_size, 8192.0)

    xs: List[float] = []
    ys: List[float] = []
    zs: List[float] = []
    for ei, ent in enumerate(parsed_map.entities):
        # Only brushes assigned to this cell contribute.
        brushes_here = entity_brushes_in_cell(ent, cell_key, chunk_size, scale)
        for brush in brushes_here:
            (mn, mx) = compute_brush_aabb(brush)
            xs.extend([mn[0], mx[0]])
            ys.extend([mn[1], mx[1]])
            zs.extend([mn[2], mx[2]])
        # Point entity origins also contribute a point.
        if not ent.brushes and cell_of(ent.origin, chunk_size, scale) == cell_key:
            xs.append(ent.origin[0]); ys.append(ent.origin[1]); zs.append(ent.origin[2])

    if not xs:
        # Map extents -> world extents:
        #   world_x = map_x * s
        #   world_y = map_z * s          (map_z is up)
        #   world_z = -map_y * s         (flipped)
        mn = (fb_min_map[0] * scale, fb_min_map[2] * scale, -fb_max_map[1] * scale)
        mx = (fb_max_map[0] * scale, fb_max_map[2] * scale, -fb_min_map[1] * scale)
        return (mn, mx)

    mn = (min(xs) * scale, min(zs) * scale, -max(ys) * scale)
    mx = (max(xs) * scale, max(zs) * scale, -min(ys) * scale)
    return (mn, mx)


def cell_id(cell_key: CellKey) -> str:
    """Stable string id for a cell, e.g. (3,5) -> 'cell_03_05'.

    Two-digit zero-padded per axis; supports up to ±99 cells. For larger
    grids the padding widens automatically (no truncation)."""
    ix, iz = cell_key
    def pad(n: int) -> str:
        s = str(abs(n))
        # pad to at least 2, wider if needed
        return s.zfill(2) if len(s) <= 2 else s
    sign_x = "n" if ix < 0 else ""
    sign_z = "n" if iz < 0 else ""
    return f"cell_{sign_x}{pad(ix)}_{sign_z}{pad(iz)}"


def neighbor_cell(cell_key: CellKey, axis: str) -> Optional[CellKey]:
    """Return the neighbor cell key along +X/-X/+Z/-Z, or None for 'no neighbor'.

    Used to build the map-pack adjacency graph. axis is one of
    '+X','-X','+Z','-Z'.
    """
    ix, iz = cell_key
    if axis == "+X":
        return (ix + 1, iz)
    if axis == "-X":
        return (ix - 1, iz)
    if axis == "+Z":
        return (ix, iz + 1)
    if axis == "-Z":
        return (ix, iz - 1)
    return None