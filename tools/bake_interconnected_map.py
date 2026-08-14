#!/usr/bin/env python3
"""Interconnected map bake orchestration (Inc 3).

Parses once, builds the canonical IR, builds world geometry, builds the global
collision scene, and partitions visual geometry into cells — producing an
in-memory `WorldBuild` before any output directory is touched.

Inc 3 produces the in-memory build + a report. Inc 4 adds the writers that
serialize it to CMSH/LVL/map-pack v2.

Usage:
    python3 tools/bake_interconnected_map.py <map> --out-dir DIR
        [--scale 0.2] [--chunk-size N] [--auto-chunk-size] [--strict]
        [--mappack-id forsyken-city]
"""

import sys
import os
import argparse
import json
import math
from pathlib import Path

# Add tools to path for imports.
sys.path.insert(0, str(Path(__file__).parent))

from ogworld.parse import build_world_ir
from ogworld.geometry import build_world_geometry
from ogworld.collision import (
    build_global_collision, collision_budget, validate_global_coverage,
)
from ogworld.chunking import partition_world, build_adjacency, cell_id
from ogworld.class_policy import validate_policies
from ogworld.distant_lod import (
    build_distant_dlod, build_distant_dlod_directional,
)
from writers.colmesh_world_writer import write_colmesh
from writers.lvl_world_writer import write_lvl_room
from mappack_format import (
    MapPackV2, V2Room, V2Spawn, write_mappack_v2_binary,
    SPAWN_START, SPAWN_ANCHOR, SPAWN_ACTOR,
)
from artifact_hash import crc32_file, artifact_hash

# Runtime caps (must match src/user/gameplay/world/level_loader.hpp).
K_MAX_FACES = 1024
K_MAX_VERTICES = 8192
# Runtime room table cap (must match MapSpec::kMaxRooms).
K_MAX_ROOMS = 64


class BakeError(Exception):
    pass


def count_visual_faces(chunk) -> int:
    """Count LVL faces a chunk would produce (one face per polygon)."""
    return len(chunk.polygons)


def count_visual_vertices(chunk) -> int:
    """Count LVL vertices a chunk would produce (sum of polygon verts)."""
    return sum(len(p.verts) for p in chunk.polygons)


def find_optimal_chunk_size(
    build, polygons, scale, eps,
    chunk_min=600.0, chunk_max=3000.0, chunk_step=50.0,
):
    """Search for the smallest chunk_size that fits runtime caps."""
    for cs in range(int(chunk_min), int(chunk_max) + 1, int(chunk_step)):
        chunks, diags = partition_world(build, polygons, float(cs), scale)
        if len(chunks) > K_MAX_ROOMS:
            continue
        if all(
            count_visual_faces(c) <= K_MAX_FACES
            and count_visual_vertices(c) <= K_MAX_VERTICES
            for c in chunks.values()
        ):
            return float(cs), chunks, diags
    raise BakeError("no chunk_size fits runtime caps")


def main() -> int:
    ap = argparse.ArgumentParser(description="Interconnected map bake")
    ap.add_argument("map", help="source .map path")
    ap.add_argument("--out-dir", required=True)
    ap.add_argument("--scale", type=float, default=0.2)
    ap.add_argument("--chunk-size", type=float, default=None)
    ap.add_argument("--auto-chunk-size", action="store_true")
    ap.add_argument("--strict", action="store_true", default=True)
    ap.add_argument("--mappack-id", default="forsyken-city")
    ap.add_argument("--eps", type=float, default=1e-4)
    ap.add_argument("--distant-budget", type=int, default=20,
                    help="per-cell distant face budget (hard ceiling; default 20)")
    ap.add_argument("--no-directional", action="store_true",
                    help="emit a single 360° distant mesh instead of 4 "
                         "per-direction silhouettes (Inc 4 fallback)")
    args = ap.parse_args()

    if args.distant_budget <= 0:
        print(f"WARN: invalid --distant-budget {args.distant_budget}; "
              f"clamping to default 20")
        args.distant_budget = 20
    args.directional = not args.no_directional

    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    # 1. Parse once into the IR.
    build = build_world_ir(args.map, scale=args.scale, eps=args.eps,
                           strict=args.strict)

    # 2. Build world geometry (single source of truth).
    polygons, geom_diags = build_world_geometry(build, scale=args.scale,
                                                eps=args.eps)

    # 3. Build the global collision scene.
    scene = build_global_collision(polygons, scale=args.scale, eps=args.eps)
    coverage_errors = validate_global_coverage(scene, polygons)
    if coverage_errors:
        raise BakeError("global collision coverage failure:\n" +
                        "\n".join(coverage_errors[:20]))

    # 4. Partition visual geometry into cells.
    if args.chunk_size is not None:
        chunks, chunk_diags = partition_world(
            build, polygons, args.chunk_size, args.scale)
    elif args.auto_chunk_size:
        args.chunk_size, chunks, chunk_diags = find_optimal_chunk_size(
            build, polygons, args.scale, args.eps)
    else:
        raise BakeError("must specify --chunk-size or --auto-chunk-size")

    build.chunks = chunks
    build.collision_tris = scene.tris
    build.world_verts = scene.verts

    # 5. Cap diagnostics. The full-map fit is an Inc 5 gate; here we record
    #    which cells exceed the runtime caps with source ids and suggested
    #    chunk sizes, rather than silently publishing an over-cap bake.
    cap_errors = []
    if len(chunks) > K_MAX_ROOMS:
        cap_errors.append(
            f"room count {len(chunks)} exceeds kMaxRooms {K_MAX_ROOMS}; "
            f"increase --chunk-size"
        )
    for c in chunks.values():
        if count_visual_faces(c) > K_MAX_FACES:
            cap_errors.append(
                f"cell {cell_id(c.cell)} faces {count_visual_faces(c)} "
                f"> cap {K_MAX_FACES}; decrease --chunk-size or clip"
            )
        if count_visual_vertices(c) > K_MAX_VERTICES:
            cap_errors.append(
                f"cell {cell_id(c.cell)} verts {count_visual_vertices(c)} "
                f"> cap {K_MAX_VERTICES}; decrease --chunk-size or clip"
            )

    # 6. Budget + report.
    budget = collision_budget(scene)
    adjacency = build_adjacency(chunks)

    # 7. Write artifacts (Inc 4): one global CMSH, per-room LVL, map-pack v2.
    #    Writes to an isolated staging dir; the published DFS dir is switched
    #    only after the v2 reader lands (Inc 6).
    staging = out_dir / "staging"
    staging.mkdir(parents=True, exist_ok=True)

    # Inc 5: the LVL2 distant path is removed — the `.dlod` is the only distant
    # artifact. Remove any stale `*_distant.lvl` from a prior bake so they
    # don't leak into the published pack.
    for stale in staging.glob("*_distant.lvl"):
        stale.unlink()

    # Global CMSH.
    global_colmesh_path = staging / f"{args.mappack_id}.colmesh"
    audit_path = staging / f"{args.mappack_id}.colmesh.audit.json"
    cmsh_stats = write_colmesh(scene, str(global_colmesh_path), str(audit_path))
    g_hash = artifact_hash(str(global_colmesh_path))

    # Per-room LVL + spawn table.
    v2_rooms: List[V2Room] = []
    # Per-cell distant stats (faces/verts/bytes) for the Inc 1 audit report.
    distant_stats_by_cell: dict = {}

    # Inc 3 / D2: compute ONE shared world origin (map AABB center) for the
    # distant pass. All distant cells pack relative to this single origin so
    # the runtime can draw the whole distant pass under ONE camera-relative
    # matrix (no per-cell origins, no per-mesh matrix rebuild). At kLodScale
    # 0.25 the full map diagonal (~2000u) packs to ~500 int16 units — far
    # inside range. The near pass keeps per-cell origins (kPosScale 32).
    map_min = [float("inf")] * 3
    map_max = [-float("inf")] * 3
    for c in chunks.values():
        amin, amax = _chunk_world_aabb(c)
        for i in range(3):
            map_min[i] = min(map_min[i], amin[i])
            map_max[i] = max(map_max[i], amax[i])
    shared_origin = tuple((map_min[i] + map_max[i]) * 0.5 for i in range(3))

    for k, c in sorted(chunks.items()):
        cid = cell_id(k)
        lvl_path = staging / f"{cid}.lvl"
        lvl_stats = write_lvl_room(c, build.texture_manifest, str(lvl_path),
                                   scale=args.scale)
        lvl_hash = artifact_hash(str(lvl_path))
        # World AABB from the cell's polygons.
        amin, amax = _chunk_world_aabb(c)
        # Render origin = the cell's world-space CENTER (XZ) and the center of
        # the cell's height AABB (Y). Using the center keeps every vertex
        # within ±cell_w/2 in X/Z and ±(amax[1]-amin[1])/2 in Y of the origin,
        # so the int16 kPosScale packing never overflows even for far cells or
        # tall mountain geometry. (Y=0 would overflow once |world.y| > ~1024.)
        cell_w = args.chunk_size * args.scale
        render_origin = ((k[0] + 0.5) * cell_w,
                         (amin[1] + amax[1]) * 0.5,
                         (k[1] + 0.5) * cell_w)
        # Compact `.dlod` (Inc 3/5): the ONLY distant artifact. Inc 4:
        # 4-direction silhouettes by default (`--no-directional` falls back to
        # a single 360° mesh). Inc 3 / D2: all cells pack relative to the
        # SHARED map-center origin (not the per-cell render_origin) so the
        # distant pass draws under one camera-relative matrix.
        dlod_path = staging / f"{cid}_distant.dlod"
        if args.directional:
            dlod_stats = build_distant_dlod_directional(
                c, len(build.texture_manifest), shared_origin, str(dlod_path),
                budget=args.distant_budget)
        else:
            dlod_stats = build_distant_dlod(
                c, len(build.texture_manifest), shared_origin, str(dlod_path),
                budget=args.distant_budget)
        if dlod_stats is None:
            # No renderable geometry — remove any stale distant file.
            if dlod_path.exists():
                dlod_path.unlink()
            distant_stats_by_cell[cid] = None
        else:
            distant_stats_by_cell[cid] = {
                "faces": dlod_stats["faces"],
                "vertices": dlod_stats["vertices"],
                "bytes": dlod_path.stat().st_size,
                "budget_met": dlod_stats["budget_met"],
            }
        # Spawn records for this cell.
        spawns = []
        for s in c.spawns:
            kind = SPAWN_START if s.kind == "start" else (
                SPAWN_ANCHOR if s.kind == "anchor" else SPAWN_ACTOR)
            spawns.append(V2Spawn(
                kind=kind, source_id=s.source_id, room_id=cid,
                position=s.position, name=s.name, classname=s.classname,
            ))
        v2_rooms.append(V2Room(
            id=cid,
            lvl_path=f"rom:/lvl/{args.mappack_id}/{cid}.lvl",
            render_origin=render_origin,
            world_aabb_min=amin, world_aabb_max=amax,
            lvl_crc32=lvl_hash["crc32"], lvl_size=lvl_hash["size_bytes"],
            neighbors={ax: (cell_id(v) if v else "")
                       for ax, v in adjacency[k].items()},
            spawns=spawns,
        ))

    # Start room = the cell containing the 'Start' spawn.
    start_room_id = ""
    for r in v2_rooms:
        if any(s.kind == SPAWN_START for s in r.spawns):
            start_room_id = r.id
            break
    if not start_room_id and v2_rooms:
        start_room_id = v2_rooms[0].id

    pack = MapPackV2(
        id=args.mappack_id,
        source_sha256=build.source_sha256,
        pipeline_version=2,
        scale=args.scale,
        chunk_size=args.chunk_size,
        global_colmesh_path=f"rom:/lvl/{args.mappack_id}/{args.mappack_id}.colmesh",
        global_colmesh_crc32=g_hash["crc32"],
        global_colmesh_size=g_hash["size_bytes"],
        global_vertex_count=cmsh_stats["vertices"],
        global_triangle_count=cmsh_stats["triangles"],
        global_bvh_node_count=cmsh_stats["bvh_nodes"],
        start_room_id=start_room_id,
        rooms=v2_rooms,
    )
    mappack_path = staging / f"{args.mappack_id}.mappack"
    write_mappack_v2_binary(pack, str(mappack_path))

    # Emit the per-pack material manifest (Inc 5): one material name per line,
    # in the SAME order as `build.texture_manifest` (the per-cell LVL string
    # ids). `MaterialCatalog` reads rom:/lvl/<pack>.manifest to resolve a
    # material id -> sprite. The order MUST match the bake's material ids so
    # the texture <-> material mapping stays consistent with
    # `LvlRoomRenderer::material_color()`.
    manifest_path = staging / f"{args.mappack_id}.manifest"
    with open(manifest_path, "w") as f:
        for mat in build.texture_manifest:
            f.write(mat + "\n")

    report = {
        "source": args.map,
        "source_sha256": build.source_sha256,
        "scale": args.scale,
        "chunk_size": args.chunk_size,
        "mappack_id": args.mappack_id,
        "polygons": len(polygons),
        "geometry_diagnostics": len(geom_diags),
        "collision": budget,
        "coverage_errors": len(coverage_errors),
        "chunk_count": len(chunks),
        "cap_errors": cap_errors,
        "global_colmesh": {
            "path": str(global_colmesh_path),
            "crc32": g_hash["crc32"],
            "size_bytes": g_hash["size_bytes"],
            "vertices": cmsh_stats["vertices"],
            "triangles": cmsh_stats["triangles"],
            "bvh_nodes": cmsh_stats["bvh_nodes"],
        },
        "start_room_id": start_room_id,
        "chunks": {
            cell_id(k): {
                "cell": list(k),
                "faces": count_visual_faces(c),
                "vertices": count_visual_vertices(c),
                "spawns": len(c.spawns),
            }
            for k, c in sorted(chunks.items())
        },
        "distant": {
            cid: stats
            for cid, stats in sorted(distant_stats_by_cell.items())
        },
        "adjacency": {
            cell_id(k): {ax: (cell_id(v) if v else "")
                         for ax, v in adj.items()}
            for k, adj in sorted(adjacency.items())
        },
        "brush_cell_counts": {
            f"{k[0]}:{k[1]}": v
            for k, v in sorted(build.brush_cell_counts.items())
        },
        "policy_summary": build.policy_summary,
    }

    report_path = out_dir / "interconnected_report.json"
    with open(report_path, "w") as f:
        json.dump(report, f, indent=2)

    print(f"PASS: {args.map} -> {len(chunks)} visual cells, "
          f"{budget['triangles']} collision tris, "
          f"{budget['vertices']} collision verts")
    print(f"PASS: global CMSH -> {cmsh_stats['triangles']} tris, "
          f"{cmsh_stats['vertices']} verts, {cmsh_stats['bvh_nodes']} BVH nodes")
    print(f"PASS: map-pack v2 -> {len(v2_rooms)} rooms, start={start_room_id}")
    if cap_errors:
        print(f"WARN: {len(cap_errors)} cap diagnostic(s) recorded "
              f"(full-map fit is an Inc 5 gate)")
    print(f"report -> {report_path}")
    return 0


def _chunk_world_aabb(chunk):
    """World-space AABB of a chunk's polygons (fallback to cell grid)."""
    xs, ys, zs = [], [], []
    for p in chunk.polygons:
        for v in p.verts:
            xs.append(v[0]); ys.append(v[1]); zs.append(v[2])
    if not xs:
        return (0.0, 0.0, 0.0), (0.0, 0.0, 0.0)
    return (
        (min(xs), min(ys), min(zs)),
        (max(xs), max(ys), max(zs)),
    )


if __name__ == "__main__":
    sys.exit(main())
