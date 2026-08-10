#!/usr/bin/env python3
"""Map-pack manifest format.

A thin manifest sitting above LVL2/colmesh. Each map-pack lists its chunk
rooms (one LVL2 + colmesh per chunk), the ±X/±Z adjacency graph, the shared
atmosphere, and the start room (the cell containing the 'Start' PlayerSpawn).

Two serializations:
  - `<pack>.mappack.json` — human-readable JSON, for host-side tests.
  - `<pack>.mappack` — compact binary, for the N64 runtime (length-prefixed
    strings + fixed arrays; no JSON parser in ROM).

The runtime reads ONLY the binary. Both are emitted by bake_map_pack.py.
"""

from __future__ import annotations

import json
import struct
from dataclasses import dataclass, field, asdict
from pathlib import Path
from typing import List, Tuple, Dict, Optional, Any

# Neighbor axis order used by the binary format (must match the C++ enum).
NEIGHBOR_AXES = ("+X", "-X", "+Z", "-Z")

# Binary magic + version.
MAGIC = b"MPPK"
VERSION = 1


@dataclass
class MapRoom:
    id: str                          # e.g. "cell_03_05"
    lvl_path: str                    # e.g. "rom:/lvl/forsaken-city/cell_03_05.lvl"
    colmesh_path: str                # e.g. "rom:/lvl/forsaken-city/cell_03_05.colmesh" or ""
    aabb_min: Tuple[float, float, float]   # world units (post-scale)
    aabb_max: Tuple[float, float, float]
    has_colmesh: bool = True
    has_start_spawn: bool = False
    start_spawn: Tuple[float, float, float] = (0.0, 0.0, 0.0)
    # Per-axis neighbor room ids; "" = no neighbor (map edge).
    neighbors: Dict[str, str] = field(default_factory=dict)
    # Per-room entity summary (counts by classname) for diagnostics.
    entities: Dict[str, int] = field(default_factory=dict)


@dataclass
class MapPack:
    id: str                          # e.g. "forsaken-city"
    rooms: List[MapRoom] = field(default_factory=list)
    start_room_id: str = ""
    scale: float = 0.2
    chunk_size: float = 1000.0       # MAP units (pre-scale) used for partition
    # Shared atmosphere (one skybox/music per map-pack).
    atmosphere: Dict[str, Any] = field(default_factory=dict)


def build_mappack(
    pack_id: str,
    chunks: List[Dict[str, Any]],          # ChunkStats dicts from chunks.json
    cell_to_entities: Dict[Tuple[int, int], List[int]],
    parsed_map,                            # ParsedMap
    scale: float,
    chunk_size: float,
    mappack_dir_name: str,                 # e.g. "forsaken-city" (DFS subdir)
) -> MapPack:
    """Build a MapPack from a chunked bake.

    `chunks` is the list of per-chunk stat dicts (from chunks.json).
    `cell_to_entities` maps each cell key -> list of entity indices into
    parsed_map.entities (from partition_parsed_map).
    `mappack_dir_name` is the DFS subdir under rom:/lvl/ where the chunk
    .lvl/.colmesh files live.
    """
    from ogmap_lib.brush_grid import cell_id, neighbor_cell, world_aabb_for_cell
    from ogmap_lib import extract_atmosphere

    # Build a quick lookup from cell_id -> cell_key.
    id_to_cell: Dict[str, Tuple[int, int]] = {}
    for c in chunks:
        id_to_cell[c["id"]] = tuple(c["cell"])

    rooms: List[MapRoom] = []
    for c in chunks:
        ck = tuple(c["cell"])
        mn, mx = world_aabb_for_cell(parsed_map, ck, chunk_size, scale)
        # DFS paths (must match filesystem/lvl/<mappack_dir_name>/ layout).
        lvl_path = f"rom:/lvl/{mappack_dir_name}/{c['id']}.lvl"
        colmesh_path = f"rom:/lvl/{mappack_dir_name}/{c['id']}.colmesh"
        has_colmesh = c["triangles"] > 0

        # Adjacency: only record neighbors that are also non-empty cells.
        neighbors: Dict[str, str] = {}
        for axis in NEIGHBOR_AXES:
            nb = neighbor_cell(ck, axis)
            if nb in id_to_cell.values():
                neighbors[axis] = cell_id(nb)
            else:
                neighbors[axis] = ""

        # Per-room entity summary: count classnames of entities in this cell.
        ent_counts: Dict[str, int] = {}
        start_spawn = (0.0, 0.0, 0.0)
        has_start_spawn = False
        for ei in cell_to_entities.get(ck, []):
            ent = parsed_map.entities[ei]
            ent_counts[ent.classname] = ent_counts.get(ent.classname, 0) + 1
            if ent.classname == "PlayerSpawn" and ent.properties.get("name", "") == "Start":
                has_start_spawn = True
                start_spawn = (
                    ent.origin[0] * scale,
                    ent.origin[2] * scale,    # Quake Z -> port Y
                    -ent.origin[1] * scale,   # Quake Y -> port -Z
                )

        rooms.append(MapRoom(
            id=c["id"],
            lvl_path=lvl_path,
            colmesh_path=colmesh_path if has_colmesh else "",
            aabb_min=mn,
            aabb_max=mx,
            has_colmesh=has_colmesh,
            has_start_spawn=has_start_spawn,
            start_spawn=start_spawn,
            neighbors=neighbors,
            entities=ent_counts,
        ))

    # Start room = the cell containing the 'Start' PlayerSpawn.
    start_room_id = ""
    for r in rooms:
        if r.has_start_spawn:
            start_room_id = r.id
            break
    if not start_room_id and rooms:
        # Fallback: first room with any PlayerSpawn.
        for r in rooms:
            if r.entities.get("PlayerSpawn", 0) > 0:
                start_room_id = r.id
                break
    if not start_room_id and rooms:
        start_room_id = rooms[0].id

    atmosphere = extract_atmosphere(parsed_map.entities)

    return MapPack(
        id=pack_id,
        rooms=rooms,
        start_room_id=start_room_id,
        scale=scale,
        chunk_size=chunk_size,
        atmosphere=atmosphere,
    )


def write_mappack_json(pack: MapPack, out_path: str) -> None:
    """Write the human-readable JSON serialization."""
    d = asdict(pack)
    with open(out_path, "w") as f:
        json.dump(d, f, indent=2)


def read_mappack_json(path: str) -> MapPack:
    """Read the JSON serialization back into a MapPack (host tests only)."""
    with open(path) as f:
        d = json.load(f)
    rooms = [MapRoom(**r) for r in d["rooms"]]
    return MapPack(
        id=d["id"],
        rooms=rooms,
        start_room_id=d["start_room_id"],
        scale=d["scale"],
        chunk_size=d["chunk_size"],
        atmosphere=d["atmosphere"],
    )


# ── Binary serialization (runtime) ──────────────────────────────────

def _pack_str(s: str, length: int) -> bytes:
    """Pack a string into a fixed-width null-padded field of `length` bytes."""
    b = s.encode("utf-8")[:length]
    return b.ljust(length, b"\x00")


def write_mappack_binary(pack: MapPack, out_path: str) -> None:
    """Write the compact binary serialization read by the N64 runtime.

    Layout (all big-endian to match existing .lvl/.colmesh):
      magic[4] = "MPPK"
      version: u16
      scale: f32
      chunk_size: f32         (MAP units, pre-scale)
      atmosphere: skybox[16], music[24], ambience[16], snow_amount:f32, snow_dir(3xf32)
      start_room_id[16]
      room_count: u16
      rooms[room_count]:
        id[16], lvl_path[64], colmesh_path[64],
        aabb_min(3xf32), aabb_max(3xf32),
        has_colmesh:u8, has_start_spawn:u8,
        start_spawn(3xf32),
        neighbors[4][16]  (+X,-X,+Z,-Z; "" = none)
    """
    atmos = pack.atmosphere
    snow_dir = atmos.get("snow_dir", (0, 0, 0))
    out = bytearray()
    out += MAGIC
    out += struct.pack(">H", VERSION)
    out += struct.pack(">f", pack.scale)
    out += struct.pack(">f", pack.chunk_size)
    out += _pack_str(atmos.get("skybox", ""), 16)
    out += _pack_str(atmos.get("music", ""), 24)
    out += _pack_str(atmos.get("ambience", ""), 16)
    out += struct.pack(">f", float(atmos.get("snow_amount", 0.0)))
    out += struct.pack(">fff", snow_dir[0], snow_dir[1], snow_dir[2])
    out += _pack_str(pack.start_room_id, 16)
    out += struct.pack(">H", len(pack.rooms))
    for r in pack.rooms:
        out += _pack_str(r.id, 16)
        out += _pack_str(r.lvl_path, 64)
        out += _pack_str(r.colmesh_path, 64)
        out += struct.pack(">fff", *r.aabb_min)
        out += struct.pack(">fff", *r.aabb_max)
        out += struct.pack(">BB", 1 if r.has_colmesh else 0, 1 if r.has_start_spawn else 0)
        out += struct.pack(">fff", *r.start_spawn)
        for axis in NEIGHBOR_AXES:
            out += _pack_str(r.neighbors.get(axis, ""), 16)
    with open(out_path, "wb") as f:
        f.write(bytes(out))


def read_mappack_binary(path: str) -> MapPack:
    """Read the binary serialization back (host tests / parity checks)."""
    with open(path, "rb") as f:
        data = f.read()
    off = 0
    magic = data[off:off + 4]; off += 4
    if magic != MAGIC:
        raise ValueError(f"bad mappack magic: {magic!r}")
    (version,) = struct.unpack_from(">H", data, off); off += 2
    (scale,) = struct.unpack_from(">f", data, off); off += 4
    (chunk_size,) = struct.unpack_from(">f", data, off); off += 4

    def read_str(n):
        nonlocal off
        raw = data[off:off + n]; off += n
        return raw.rstrip(b"\x00").decode("utf-8")

    skybox = read_str(16); music = read_str(24); ambience = read_str(16)
    (snow_amount,) = struct.unpack_from(">f", data, off); off += 4
    sx, sy, sz = struct.unpack_from(">fff", data, off); off += 12
    start_room_id = read_str(16)
    (room_count,) = struct.unpack_from(">H", data, off); off += 2

    rooms: List[MapRoom] = []
    for _ in range(room_count):
        rid = read_str(16)
        lvl_path = read_str(64)
        colmesh_path = read_str(64)
        amin = struct.unpack_from(">fff", data, off); off += 12
        amax = struct.unpack_from(">fff", data, off); off += 12
        has_colmesh, has_start_spawn = struct.unpack_from(">BB", data, off); off += 2
        sp = struct.unpack_from(">fff", data, off); off += 12
        neighbors = {}
        for axis in NEIGHBOR_AXES:
            neighbors[axis] = read_str(16)
        rooms.append(MapRoom(
            id=rid, lvl_path=lvl_path, colmesh_path=colmesh_path,
            aabb_min=tuple(amin), aabb_max=tuple(amax),
            has_colmesh=bool(has_colmesh), has_start_spawn=bool(has_start_spawn),
            start_spawn=tuple(sp), neighbors=neighbors,
        ))

    return MapPack(
        id="",  # binary doesn't store the pack id; caller knows it
        rooms=rooms,
        start_room_id=start_room_id,
        scale=scale,
        chunk_size=chunk_size,
        atmosphere={
            "skybox": skybox, "music": music, "ambience": ambience,
            "snow_amount": snow_amount, "snow_dir": (sx, sy, sz),
        },
    )


# ── Map-pack v2 (Inc 4) ─────────────────────────────────────────────
#
# v2 carries the metadata the v1 manifest loses: source-map hash, pipeline
# version, one global collision path/hash/counts, per-visual-room render
# origins, world AABBs, artifact hashes/counts, neighbor ids, and a fixed
# spawn table containing named Start/anchor/actor records.
#
# Binary layout (all big-endian, fixed-width for N64):
#   magic[4] = "MPP2"
#   version: u16 = 2
#   pipeline_version: u16
#   source_sha256[64]        (hex string)
#   scale: f32
#   chunk_size: f32
#   global_colmesh_path[64]
#   global_colmesh_crc32: u32
#   global_colmesh_size: u32
#   global_vertex_count: u32
#   global_triangle_count: u32
#   global_bvh_node_count: u32
#   start_room_id[16]
#   room_count: u16
#   rooms[room_count]:
#     id[16], lvl_path[64],
#     render_origin(3xf32),
#     world_aabb_min(3xf32), world_aabb_max(3xf32),
#     lvl_crc32: u32, lvl_size: u32,
#     neighbors[4][16]  (+X,-X,+Z,-Z; "" = none)
#     spawn_count: u16
#   spawns[...] (all rooms' spawns, in room order):
#     kind: u8 (0=start,1=anchor,2=actor)
#     source_id: u32
#     room_id[16]
#     position(3xf32)
#     name[32]
#     classname[32]

MAGIC_V2 = b"MPP2"
VERSION_V2 = 2
PIPELINE_VERSION = 2

# Spawn kinds.
SPAWN_START = 0
SPAWN_ANCHOR = 1
SPAWN_ACTOR = 2


@dataclass
class V2Spawn:
    kind: int
    source_id: int
    room_id: str
    position: Tuple[float, float, float]
    name: str
    classname: str


@dataclass
class V2Room:
    id: str
    lvl_path: str
    render_origin: Tuple[float, float, float]
    world_aabb_min: Tuple[float, float, float]
    world_aabb_max: Tuple[float, float, float]
    lvl_crc32: int
    lvl_size: int
    neighbors: Dict[str, str]
    spawns: List[V2Spawn] = field(default_factory=list)


@dataclass
class MapPackV2:
    id: str
    source_sha256: str
    pipeline_version: int
    scale: float
    chunk_size: float
    global_colmesh_path: str
    global_colmesh_crc32: int
    global_colmesh_size: int
    global_vertex_count: int
    global_triangle_count: int
    global_bvh_node_count: int
    start_room_id: str
    rooms: List[V2Room] = field(default_factory=list)


def write_mappack_v2_binary(pack: MapPackV2, out_path: str) -> None:
    """Write the v2 binary serialization read by the N64 runtime."""
    out = bytearray()
    out += MAGIC_V2
    out += struct.pack(">H", VERSION_V2)
    out += struct.pack(">H", pack.pipeline_version)
    out += _pack_str(pack.source_sha256, 64)
    out += struct.pack(">f", pack.scale)
    out += struct.pack(">f", pack.chunk_size)
    out += _pack_str(pack.global_colmesh_path, 64)
    out += struct.pack(">I", pack.global_colmesh_crc32)
    out += struct.pack(">I", pack.global_colmesh_size)
    out += struct.pack(">I", pack.global_vertex_count)
    out += struct.pack(">I", pack.global_triangle_count)
    out += struct.pack(">I", pack.global_bvh_node_count)
    out += _pack_str(pack.start_room_id, 16)
    out += struct.pack(">H", len(pack.rooms))
    for r in pack.rooms:
        out += _pack_str(r.id, 16)
        out += _pack_str(r.lvl_path, 64)
        out += struct.pack(">fff", *r.render_origin)
        out += struct.pack(">fff", *r.world_aabb_min)
        out += struct.pack(">fff", *r.world_aabb_max)
        out += struct.pack(">I", r.lvl_crc32)
        out += struct.pack(">I", r.lvl_size)
        for axis in NEIGHBOR_AXES:
            out += _pack_str(r.neighbors.get(axis, ""), 16)
        out += struct.pack(">H", len(r.spawns))
    for r in pack.rooms:
        for s in r.spawns:
            out += struct.pack(">B", s.kind)
            out += struct.pack(">I", s.source_id)
            out += _pack_str(s.room_id, 16)
            out += struct.pack(">fff", *s.position)
            out += _pack_str(s.name, 32)
            out += _pack_str(s.classname, 32)
    with open(out_path, "wb") as f:
        f.write(bytes(out))


def read_mappack_v2_binary(path: str) -> MapPackV2:
    """Read the v2 binary serialization back (host tests / parity checks)."""
    with open(path, "rb") as f:
        data = f.read()
    off = 0
    magic = data[off:off + 4]; off += 4
    if magic != MAGIC_V2:
        raise ValueError(f"bad mappack v2 magic: {magic!r}")
    (version,) = struct.unpack_from(">H", data, off); off += 2
    if version != VERSION_V2:
        raise ValueError(f"mappack v2 version {version} != {VERSION_V2}")
    (pipeline_version,) = struct.unpack_from(">H", data, off); off += 2

    def read_str(n):
        nonlocal off
        raw = data[off:off + n]; off += n
        return raw.rstrip(b"\x00").decode("utf-8")

    source_sha256 = read_str(64)
    (scale,) = struct.unpack_from(">f", data, off); off += 4
    (chunk_size,) = struct.unpack_from(">f", data, off); off += 4
    global_colmesh_path = read_str(64)
    (g_crc,) = struct.unpack_from(">I", data, off); off += 4
    (g_size,) = struct.unpack_from(">I", data, off); off += 4
    (g_verts,) = struct.unpack_from(">I", data, off); off += 4
    (g_tris,) = struct.unpack_from(">I", data, off); off += 4
    (g_bvh,) = struct.unpack_from(">I", data, off); off += 4
    start_room_id = read_str(16)
    (room_count,) = struct.unpack_from(">H", data, off); off += 2

    rooms: List[V2Room] = []
    for _ in range(room_count):
        rid = read_str(16)
        lvl_path = read_str(64)
        ro = struct.unpack_from(">fff", data, off); off += 12
        amin = struct.unpack_from(">fff", data, off); off += 12
        amax = struct.unpack_from(">fff", data, off); off += 12
        (lvl_crc,) = struct.unpack_from(">I", data, off); off += 4
        (lvl_size,) = struct.unpack_from(">I", data, off); off += 4
        neighbors = {}
        for axis in NEIGHBOR_AXES:
            neighbors[axis] = read_str(16)
        (spawn_count,) = struct.unpack_from(">H", data, off); off += 2
        rooms.append(V2Room(
            id=rid, lvl_path=lvl_path,
            render_origin=tuple(ro),
            world_aabb_min=tuple(amin), world_aabb_max=tuple(amax),
            lvl_crc32=lvl_crc, lvl_size=lvl_size,
            neighbors=neighbors,
            spawns=[V2Spawn(0, 0, "", (0, 0, 0), "", "") for _ in range(spawn_count)],
        ))

    for r in rooms:
        for i in range(len(r.spawns)):
            (kind,) = struct.unpack_from(">B", data, off); off += 1
            (source_id,) = struct.unpack_from(">I", data, off); off += 4
            room_id = read_str(16)
            pos = struct.unpack_from(">fff", data, off); off += 12
            name = read_str(32)
            classname = read_str(32)
            r.spawns[i] = V2Spawn(kind, source_id, room_id, tuple(pos), name, classname)

    return MapPackV2(
        id="",
        source_sha256=source_sha256,
        pipeline_version=pipeline_version,
        scale=scale,
        chunk_size=chunk_size,
        global_colmesh_path=global_colmesh_path,
        global_colmesh_crc32=g_crc,
        global_colmesh_size=g_size,
        global_vertex_count=g_verts,
        global_triangle_count=g_tris,
        global_bvh_node_count=g_bvh,
        start_room_id=start_room_id,
        rooms=rooms,
    )