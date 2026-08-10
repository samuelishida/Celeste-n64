#!/usr/bin/env python3
"""Map-pack bake CLI.

Partitions one OG .map into spatial grid chunks (2D XZ), bakes each chunk
through the existing lvl_writer/colmesh_writer (UNCHANGED), and emits a
chunks.json inventory. Each chunk is a normal single-room LVL2 + colmesh
that fits the runtime caps (kMaxFaces=1024, kMaxVertices=8192).

The per-chunk bake reuses tools/writers/lvl_writer.write_lvl and
tools/writers/colmesh_writer.write_colmesh unchanged, so the
og-map-polygon-winding guards carry over per chunk.

Usage:
    python3 tools/bake_map_pack.py <room.map> [--out-dir DIR]
        [--scale 0.2] [--eps 1e-4] [--chunk-size 1200] [--strict]
        [--submap] [--fixture-manifest PATH] [--reuse-colmesh]

The grid is 2D in WORLD XZ (cell iz = floor(world_z / (chunk_size*scale)),
world_z = -map_y) so the partition matches the runtime
Map::ResolveCellByPosition exactly. chunk_size is the cell size in MAP
units; the default 1200 fits 1.map (47 rooms, max 891 faces / 3412 verts).
"""

import sys
import os
import argparse
import hashlib
import json
import shutil
import struct
import tempfile
from pathlib import Path
from datetime import datetime
from typing import Optional, List, Dict, Tuple

# Add tools to path for imports
sys.path.insert(0, str(Path(__file__).parent))

from ogmap_lib import (
    parse_map, validate_scene, classify_entity, is_skipped,
    ParsedMap, Entity, Brush, extract_atmosphere,
)
from ogmap_lib.brush_grid import (
    CellKey, partition_parsed_map, entity_brushes_in_cell,
    world_aabb_for_cell, cell_id, neighbor_cell,
)
from writers.colmesh_writer import write_colmesh, ColmeshStats
from writers.lvl_writer import write_lvl, LvlStats
from mappack_format import (
    MapPack, build_mappack, write_mappack_json, write_mappack_binary,
)


# Runtime caps (must match src/user/gameplay/world/level_loader.hpp).
K_MAX_FACES = 1024
K_MAX_VERTICES = 8192
# Runtime room table cap (must match MapSpec::kMaxRooms in mappack_loader.hpp).
K_MAX_ROOMS = 64


class BakeError(Exception):
    """Pipeline error with non-zero exit status."""
    pass


class ChunkStats:
    def __init__(self):
        self.cell_key: CellKey = (0, 0)
        self.id: str = ""
        self.faces: int = 0
        self.vertices: int = 0
        self.triangles: int = 0
        self.bvh_nodes: int = 0
        self.entities: int = 0
        self.nav_platforms: int = 0
        self.brush_count: int = 0
        self.fits_caps: bool = False
        self.sha256: str = ""


def compute_sha256(path: str) -> str:
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for chunk in iter(lambda: f.read(8192), b''):
            h.update(chunk)
    return h.hexdigest()


def read_colmesh_counts(path: Path) -> Optional[Dict[str, int]]:
    """Parse a .colmesh CMSH header for its real vertex/triangle/BVH counts.

    Returns None if the file is missing, has a bad magic, or is truncated.
    Layout: CollHeader in src/user/gameplay/physics/coll_mesh.hpp (big-endian):
    magic(4) version:u16 flags:u16 aabb_min:3xi16 aabb_max:3xi16
    quant_scale:f32 quant_origin:3xf32 then vertex_count u32 @ 0x24,
    triangle_count u32 @ 0x28, bvh_node_count u32 @ 0x2C. Header is 68 bytes.
    """
    try:
        with open(path, "rb") as f:
            head = f.read(68)
    except OSError:
        return None
    if len(head) < 68 or head[:4] != b"CMSH":
        return None
    version, flags = struct.unpack(">HH", head[4:8])
    vertex_count, triangle_count, bvh_node_count = struct.unpack(
        ">III", head[0x24:0x30]
    )
    return {
        "version": version,
        "flags": flags,
        "vertices": vertex_count,
        "triangles": triangle_count,
        "bvh_nodes": bvh_node_count,
    }


def build_chunk_submap(
    parsed_map: ParsedMap,
    entity_indices: List[int],
    cell_key: CellKey,
    chunk_size: float,
    scale: float,
) -> ParsedMap:
    """Build a ParsedMap view containing only the entities/brushes for one cell.

    For brush-bearing entities, a per-cell sub-Entity is created carrying only
    the brushes whose AABB center falls in cell_key (worldspawn brushes are
    distributed per-brush). Point entities are included whole (their origin is
    in the cell by construction of partition_parsed_map).

    The returned ParsedMap preserves worldspawn's properties (atmosphere) by
    copying the worldspawn entity (with only its cell brushes) so the lvl
    writer's apply_atmosphere still finds it.
    """
    cell_entities: List[Entity] = []
    for ei in entity_indices:
        src = parsed_map.entities[ei]
        if src.brushes:
            sub = Entity(classname=src.classname, origin=src.origin)
            sub.properties = dict(src.properties)
            sub.brushes = entity_brushes_in_cell(src, cell_key, chunk_size, scale)
            if sub.brushes:
                cell_entities.append(sub)
            # Point entities with brushes here but no brushes in this cell are
            # skipped (they contribute nothing to this chunk).
        else:
            cell_entities.append(src)

    return ParsedMap(
        entities=cell_entities,
        textures=list(parsed_map.textures),
        world_range=parsed_map.world_range,
    )


def write_chunk(
    chunk_submap: ParsedMap,
    out_dir: Path,
    cell_id_str: str,
    scale: float,
    eps: float,
    strict: bool,
    reuse_colmesh: bool = False,
) -> Tuple[ChunkStats, Path, Path]:
    """Bake one chunk to out_dir, return stats + (lvl_path, colmesh_path).

    Uses the existing writers UNCHANGED. Atomic per-chunk publish via temp.

    Colmesh reuse is OPT-IN (--reuse-colmesh). When off (default), a
    decoration-only chunk that produces no collision will have any pre-existing
    .colmesh at the target path explicitly unlinked, so a stale mesh from a
    previous bake can never leak into the DFS. When on, the existing file's
    real triangle/BVH counts are parsed from the CMSH header (no hardcoded
    stats); an unparseable reused file is treated as no-colmesh.
    """
    stats = ChunkStats()
    stats.id = cell_id_str
    # cell_key is set by the caller before write_chunk returns into all_stats;
    # populate here too for completeness.
    # (cell_key passed via the stats object after write.)

    lvl_path = out_dir / f"{cell_id_str}.lvl"
    colmesh_path = out_dir / f"{cell_id_str}.colmesh"

    # Write to temp then rename (atomic per-chunk).
    with tempfile.TemporaryDirectory() as tmpdir:
        tmp = Path(tmpdir)
        tmp_lvl = tmp / f"{cell_id_str}.lvl"
        tmp_colmesh = tmp / f"{cell_id_str}.colmesh"

        lvl_stats = write_lvl(
            chunk_submap, str(tmp_lvl),
            scale=scale, eps=eps, strict=strict,
        )
        colmesh_stats = write_colmesh(
            chunk_submap, str(tmp_colmesh),
            scale=scale, eps=eps, strict=strict,
        )

        stats.faces = lvl_stats.faces
        stats.vertices = lvl_stats.vertices
        stats.entities = lvl_stats.entities
        stats.triangles = colmesh_stats.triangles
        stats.bvh_nodes = colmesh_stats.bvh_nodes
        stats.fits_caps = (stats.faces <= K_MAX_FACES and stats.vertices <= K_MAX_VERTICES)

        # Count brushes in the submap for containment parity.
        stats.brush_count = sum(len(e.brushes) for e in chunk_submap.entities)

        # LVL is always written; colmesh may be skipped by the writer if no
        # collision triangles were generated (e.g. a Decoration-only chunk).
        shutil.copy(str(tmp_lvl), str(lvl_path))
        if tmp_colmesh.exists():
            shutil.copy(str(tmp_colmesh), str(colmesh_path))
        elif colmesh_path.exists():
            if reuse_colmesh:
                # Opt-in reuse: keep the existing file but report its REAL
                # counts (parsed from the CMSH header) — never hardcoded.
                counts = read_colmesh_counts(colmesh_path)
                if counts is not None:
                    print(f"[map_pack] REUSE existing colmesh for {cell_id_str} "
                          f"(version={counts['version']})")
                    stats.triangles = counts["triangles"]
                    stats.bvh_nodes = counts["bvh_nodes"]
                    stats.vertices = max(stats.vertices, counts["vertices"])
                else:
                    # Unparseable colmesh: treat as no-colmesh rather than
                    # silently shipping an unknown file.
                    print(f"[map_pack] WARN: existing colmesh for {cell_id_str} "
                          f"not parseable — unlinking (no reuse)")
                    colmesh_path.unlink()
                    colmesh_path = None  # type: ignore
            else:
                # Default: never silently reuse a stale colmesh. A leftover
                # file from a previous bake (different axis/chunk-size) would
                # otherwise ship wrong collision into the DFS.
                print(f"[map_pack] WARN: stale colmesh {colmesh_path.name} "
                      f"unlinked (no --reuse-colmesh)")
                colmesh_path.unlink()
                colmesh_path = None  # type: ignore
        else:
            # No collision geometry for this chunk; do not emit a colmesh.
            colmesh_path = None  # type: ignore

    stats.sha256 = compute_sha256(str(lvl_path))
    return stats, lvl_path, colmesh_path


def find_start_room(
    parsed_map: ParsedMap,
    cell_map: Dict[CellKey, List[int]],
    chunk_size: float,
    scale: float,
) -> CellKey:
    """Find the cell containing the PlayerSpawn named 'Start'.

    OG PlayerSpawn entities carry a 'name' property; the start spawn is
    named 'Start'. If none is found, fall back to the first PlayerSpawn.
    Raises BakeError if there are no PlayerSpawn entities at all.
    """
    start_ei: Optional[int] = None
    first_spawn_ei: Optional[int] = None
    for ei, ent in enumerate(parsed_map.entities):
        if ent.classname == "PlayerSpawn":
            if first_spawn_ei is None:
                first_spawn_ei = ei
            if ent.properties.get("name", "") == "Start":
                start_ei = ei
                break
    if start_ei is None:
        start_ei = first_spawn_ei
    if start_ei is None:
        raise BakeError("No PlayerSpawn entity in map; cannot pick start room")

    from ogmap_lib.brush_grid import cell_of
    # PlayerSpawn is a point entity; origin determines its cell.
    origin = parsed_map.entities[start_ei].origin
    return cell_of(origin, chunk_size, scale)


def find_optimal_chunk_size(parsed_map: ParsedMap, scale: float, eps: float = 1e-4,
                           chunk_min: float = 600.0, chunk_max: float = 3000.0,
                           chunk_step: float = 50.0) -> float:
    """Binary-style search for the smallest chunk_size that fits runtime caps.

    Returns the smallest chunk_size in [chunk_min, chunk_max] that yields
    <= kMaxRooms cells. The search goes from small to large (step=+chunk_step)
    and stops at the first size that fits.

    Raises BakeError if no size fits (even chunk_max exceeds kMaxRooms).
    """
    print(f"[map_pack] Searching for optimal chunk_size (range: {chunk_min}–{chunk_max}, step={chunk_step})")
    candidates = []
    cs = chunk_min
    while cs <= chunk_max + 1e-6:
        candidates.append(cs)
        cs += chunk_step

    for cs in candidates:
        cell_map = partition_parsed_map(parsed_map, cs, scale)
        non_empty = sorted(cell_map.keys())
        if len(non_empty) <= K_MAX_ROOMS:
            print(f"[map_pack] Optimal chunk_size={cs:.0f} yields {len(non_empty)} rooms (kMaxRooms={K_MAX_ROOMS})")
            return cs

    # No size fit — report the tightest failure.
    tightest = chunk_max
    tightest_count = None
    for cs in candidates:
        cell_map = partition_parsed_map(parsed_map, cs, scale)
        count = len(sorted(cell_map.keys()))
        if count < tightest_count if tightest_count is not None else 0:
            tightest = cs
            tightest_count = count
    raise BakeError(
        f"No chunk_size fits kMaxRooms={K_MAX_ROOMS} in range {chunk_min}–{chunk_max}. "
        f"Tightest failure: chunk_size={tightest:.0f} yields {tightest_count} rooms. "
        f"Reduce map scope or increase kMaxRooms."
    )


def bake_map_pack(
    input_path: str,
    out_dir: str,
    scale: float = 0.2,
    eps: float = 1e-4,
    chunk_size: float = 1200.0,
    strict: bool = False,
    is_submap: bool = False,
    mappack_id: Optional[str] = None,
    reuse_colmesh: bool = False,
    parsed_map: Optional[ParsedMap] = None,
) -> List[ChunkStats]:
    """Partition and bake a map into grid chunks.

    Returns per-chunk ChunkStats. Raises BakeError on cap/overflow violations.
    If parsed_map is provided, it is used instead of re-parsing input_path.
    """
    print(f"[map_pack] parsing {input_path}")
    if parsed_map is None:
        parsed_map = parse_map(input_path)
    input_sha = compute_sha256(input_path)

    print(f"[map_pack] validating scene")
    is_valid, messages = validate_scene(parsed_map, eps, strict)
    if strict and not is_valid:
        raise BakeError("Scene validation failed:\n" + "\n".join(messages))

    # 1. Partition into grid cells (2D WORLD XZ — matches runtime resolution).
    cell_map = partition_parsed_map(parsed_map, chunk_size, scale)
    non_empty = sorted(cell_map.keys())

    # B-side submap mode: bake the whole map as ONE chunk (no grid partition),
    # and guard it fits single-room caps. A B-side that exceeds single-room
    # caps is a deferred multi-chunk B-side (needs the multi-room runtime).
    if is_submap:
        non_empty = [(0, 0)]  # single synthetic cell holding the whole map
        cell_map = {(0, 0): list(range(len(parsed_map.entities)))}

    print(f"[map_pack] {len(non_empty)} non-empty cells (chunk_size={chunk_size})")

    if len(non_empty) > K_MAX_ROOMS:
        raise BakeError(
            f"Non-empty cell count {len(non_empty)} exceeds runtime kMaxRooms "
            f"({K_MAX_ROOMS}). Use a larger --chunk-size (fewer, bigger cells)."
        )

    # B-side single-room guard: a submap must fit single-room caps (checked
    # per-chunk below after baking; >1 cell is impossible in submap mode).

    out = Path(out_dir)
    out.mkdir(parents=True, exist_ok=True)

    all_stats: List[ChunkStats] = []
    for cell_key in non_empty:
        cid = cell_id(cell_key)
        entity_indices = cell_map[cell_key]
        if is_submap:
            # Submap mode: one chunk holds the whole map (no brush filtering).
            submap = parsed_map
        else:
            submap = build_chunk_submap(parsed_map, entity_indices, cell_key, chunk_size, scale)
        print(f"[map_pack] baking chunk {cid} ({len(entity_indices)} entities)")
        stats, lvl_path, colmesh_path = write_chunk(
            submap, out, cid, scale, eps, strict,
            reuse_colmesh=reuse_colmesh,
        )
        stats.cell_key = cell_key
        if not stats.fits_caps:
            if is_submap:
                raise BakeError(
                    f"B-side {cid} exceeds single-room caps: faces={stats.faces} "
                    f"(cap {K_MAX_FACES}), vertices={stats.vertices} "
                    f"(cap {K_MAX_VERTICES}). Multi-chunk B-side deferred — needs "
                    f"the multi-room runtime. Reduce map scope."
                )
            raise BakeError(
                f"Chunk {cid} exceeds runtime caps: faces={stats.faces} "
                f"(cap {K_MAX_FACES}), vertices={stats.vertices} "
                f"(cap {K_MAX_VERTICES}). Use a smaller --chunk-size."
            )
        all_stats.append(stats)

    # 2. Emit chunks.json inventory.
    inventory = {
        "input_sha256": input_sha,
        "scale": scale,
        "eps": eps,
        "chunk_size": chunk_size,
        "strict": strict,
        "is_submap": is_submap,
        "timestamp": datetime.now().isoformat(),
        "chunk_count": len(all_stats),
        "chunks": [
            {
                "id": s.id,
                "cell": list(s.cell_key),
                "faces": s.faces,
                "vertices": s.vertices,
                "triangles": s.triangles,
                "bvh_nodes": s.bvh_nodes,
                "entities": s.entities,
                "brush_count": s.brush_count,
                "fits_caps": s.fits_caps,
                "sha256": s.sha256,
            }
            for s in all_stats
        ],
    }
    with open(out / "chunks.json", "w") as f:
        json.dump(inventory, f, indent=2)

    # 3. Build and emit the map-pack manifest (JSON + binary).
    #    The DFS subdir name is derived from the input map filename
    #    (e.g. "1.map" -> "forsaken-city" via the --mappack-id flag, default
    #    "forsaken-city" for the A-side; for a B-side it is the map stem).
    pack_id = mappack_id if mappack_id else Path(input_path).stem
    cell_to_entities = partition_parsed_map(parsed_map, chunk_size, scale)
    if is_submap:
        # Submap: single synthetic cell holding all entities.
        cell_to_entities = {(0, 0): list(range(len(parsed_map.entities)))}
    chunk_dicts = [inventory["chunks"][i] for i in range(len(all_stats))]
    pack = build_mappack(
        pack_id=pack_id,
        chunks=chunk_dicts,
        cell_to_entities=cell_to_entities,
        parsed_map=parsed_map,
        scale=scale,
        chunk_size=chunk_size,
        mappack_dir_name=pack_id,
    )
    json_path = out / f"{pack_id}.mappack.json"
    bin_path = out / f"{pack_id}.mappack"
    write_mappack_json(pack, str(json_path))
    write_mappack_binary(pack, str(bin_path))
    print(f"[map_pack] manifest: {json_path.name} + {bin_path.name} "
          f"(start_room={pack.start_room_id}, {len(pack.rooms)} rooms)")

    print(f"[map_pack] done: {len(all_stats)} chunks in {out_dir}")
    return all_stats


def main():
    parser = argparse.ArgumentParser(
        description="Bake OG map into grid-chunked map-pack (multi-room)."
    )
    parser.add_argument("input", help="Input .map file")
    parser.add_argument("--out-dir", default="build", help="Output directory")
    parser.add_argument("--scale", type=float, default=0.2, help="World scale factor")
    parser.add_argument("--eps", type=float, default=1e-4, help="Geometry tolerance")
    parser.add_argument("--chunk-size", type=float, default=1200.0,
                        help="Grid cell size in map units (default 1200 for 1.map)")
    parser.add_argument("--strict", action="store_true", help="Fail on invalid brushes")
    parser.add_argument("--submap", action="store_true",
                        help="Bake as a single-room B-side submap (errors if >1 chunk)")
    parser.add_argument("--mappack-id", default=None,
                        help="Map-pack id / DFS subdir name (default: input stem)")
    parser.add_argument("--reuse-colmesh", action="store_true",
                        help="Opt-in reuse of existing .colmesh for decoration-only chunks "
                             "(default: stale colmesh files are unlinked)")
    parser.add_argument("--fixture-manifest", help="Path to fixture manifest (opt-in)")
    parser.add_argument("--auto-chunk-size", action="store_true",
                        help="Search for the smallest chunk_size that fits caps "
                             "(default: use --chunk-size)")
    args = parser.parse_args()

    try:
        chunk_size = args.chunk_size
        parsed = None
        if args.auto_chunk_size:
            # Parse once; find_optimal_chunk_size only partitions (no file I/O beyond parse).
            parsed = parse_map(args.input)
            chunk_size = find_optimal_chunk_size(parsed, args.scale, args.eps)
        bake_map_pack(
            input_path=args.input,
            parsed_map=parsed,  # pass pre-parsed to avoid re-parse
            out_dir=args.out_dir,
            scale=args.scale,
            eps=args.eps,
            chunk_size=args.chunk_size,
            strict=args.strict,
            is_submap=args.submap,
            mappack_id=args.mappack_id,
            reuse_colmesh=args.reuse_colmesh,
        )
        return 0
    except BakeError as e:
        print(f"[map_pack] error: {e}")
        return 1


if __name__ == "__main__":
    sys.exit(main())