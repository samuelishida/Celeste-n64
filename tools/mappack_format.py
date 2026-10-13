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
import math
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
    """Pack a string into a fixed-width null-padded field of `length` bytes.

    Raises instead of silently truncating: the C++ reader forces
    dst[length - 1] = '\0', so the effective maximum is `length - 1` bytes and
    a truncated value would resolve to a different (wrong) path at runtime.
    """
    b = s.encode("utf-8")
    if len(b) >= length:
        raise ValueError(
            f"field {s!r} is {len(b)} bytes; max {length - 1} for a "
            f"{length}-byte fixed field"
        )
    return b.ljust(length, b"\x00")


def _pack_fixed(s: str, length: int) -> bytes:
    """Pack an exact-width field with no terminator (e.g. a sha256 digest).

    Unlike `_pack_str` this tolerates a value that fills the field exactly,
    which is legal for a full-length digest.
    """
    b = s.encode("utf-8")
    if len(b) > length:
        raise ValueError(f"field {s!r} is {len(b)} bytes; max {length}")
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


# ── Map-pack v2/v3/v4 (runtime manifest) ─────────────────────────────
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
MAGIC_V3 = b"MPP3"
VERSION_V3 = 3
MAGIC_V4 = b"MPP4"
VERSION_V4 = 4
MAGIC_V5 = b"MPP5"
VERSION_V5 = 5
PIPELINE_VERSION = 2
DYNAMIC_SOLID_MISSING_TARGET = 0xFFFFFFFF

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
    actor_flags: int = 0  # MPP3: bit 0 = double refill


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
class V4Hazard:
    kind: int  # 1 = DeathBlock, 2 = SpikeBlock
    source_id: int
    bounds_min: Tuple[float, float, float]
    bounds_max: Tuple[float, float, float]
    spike_direction: Tuple[float, float, float] = (0.0, 0.0, 0.0)


@dataclass
class V5DynamicSolid:
    source_id: int
    solid_id: int
    kind: int
    flags: int
    position: Tuple[float, float, float]
    yaw_radians: float
    cms_first_source_face_id: int
    cms_face_count: int
    dsol_first_face: int
    dsol_face_count: int
    dsol_first_vertex: int
    dsol_vertex_count: int
    target_source_id: int = DYNAMIC_SOLID_MISSING_TARGET
    target_position: Tuple[float, float, float] = (0.0, 0.0, 0.0)


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
    hazards: List[V4Hazard] = field(default_factory=list)
    dsol_path: str = ""
    dsol_crc32: int = 0
    dsol_size: int = 0
    dynamic_solids: List[V5DynamicSolid] = field(default_factory=list)


# The v3/v4 record shape adds actor_flags; v4 also appends hazard volumes.
V3Spawn = V2Spawn
V3Room = V2Room
MapPackV3 = MapPackV2
MapPackV4 = MapPackV2
MapPackV5 = MapPackV2
V4Spawn = V2Spawn
V4Room = V2Room
V5Spawn = V2Spawn
V5Room = V2Room


def _validate_hazard(hazard: V4Hazard) -> None:
    if hazard.kind not in (1, 2):
        raise ValueError(f"invalid hazard kind: {hazard.kind}")
    if not isinstance(hazard.source_id, int) or not 0 <= hazard.source_id <= 0xFFFFFFFF:
        raise ValueError(f"invalid hazard source_id: {hazard.source_id!r}")
    if len(hazard.bounds_min) != 3 or len(hazard.bounds_max) != 3:
        raise ValueError("hazard bounds must contain three coordinates")
    values = (*hazard.bounds_min, *hazard.bounds_max, *hazard.spike_direction)
    if len(values) != 9 or not all(math.isfinite(value) for value in values):
        raise ValueError("hazard record contains non-finite or malformed coordinates")
    if any(lo > hi for lo, hi in zip(hazard.bounds_min, hazard.bounds_max)):
        raise ValueError("hazard bounds minimum exceeds maximum")
    if hazard.kind == 1 and any(value != 0.0 for value in hazard.spike_direction):
        raise ValueError("DeathBlock must not carry a spike direction")
    if hazard.kind == 2:
        length2 = sum(value * value for value in hazard.spike_direction)
        if abs(length2 - 1.0) > 1e-4:
            raise ValueError("SpikeBlock direction must be unit length")


def _validate_spawn_flags(spawn: V2Spawn) -> None:
    flags = spawn.actor_flags
    if not isinstance(flags, int) or flags < 0 or flags > 0xFF:
        raise ValueError(f"invalid actor_flags value: {flags!r}")
    if flags & ~0x01:
        raise ValueError(f"unknown actor_flags bits: 0x{flags:02x}")
    if flags and (spawn.kind != SPAWN_ACTOR or spawn.classname != "Refill"):
        raise ValueError("refill actor flags require an actor-class Refill spawn")


def _validate_dynamic_solid(record: V5DynamicSolid) -> None:
    if not 0 <= record.source_id <= 0xFFFFFFFF:
        raise ValueError("dynamic-solid source id outside uint32")
    if not 0 <= record.solid_id <= 0xFFFF:
        raise ValueError("dynamic-solid OG solid id outside uint16")
    if not 1 <= record.kind <= 8:
        raise ValueError("invalid dynamic-solid kind")
    if record.flags & ~0x01:
        raise ValueError("unknown dynamic-solid flags")
    if record.flags and record.kind not in (2, 4):
        raise ValueError("dynamic-solid flags are invalid for this class")
    if len(record.position) != 3 or len(record.target_position) != 3:
        raise ValueError("dynamic-solid positions must contain three coordinates")
    if not all(math.isfinite(value) for value in
               (*record.position, record.yaw_radians, *record.target_position)):
        raise ValueError("dynamic-solid transform contains non-finite values")
    if not 1 <= record.cms_first_source_face_id <= 0xFFFF:
        raise ValueError("dynamic-solid CMSH first face id outside range")
    if not 1 <= record.cms_face_count <= 0xFFFF or \
            record.cms_first_source_face_id + record.cms_face_count - 1 > 0xFFFF:
        raise ValueError("dynamic-solid CMSH face range outside uint16")
    for first, count in ((record.dsol_first_face, record.dsol_face_count),
                         (record.dsol_first_vertex, record.dsol_vertex_count)):
        if not 0 <= first <= 0xFFFFFFFF or not 1 <= count <= 0xFFFFFFFF or \
                first + count > 0xFFFFFFFF:
            raise ValueError("dynamic-solid DSOL range outside uint32")
    if record.target_source_id != DYNAMIC_SOLID_MISSING_TARGET and not \
            0 <= record.target_source_id <= 0xFFFFFFFF:
        raise ValueError("dynamic-solid target source id outside uint32")
    if record.target_source_id == DYNAMIC_SOLID_MISSING_TARGET and any(
            value != 0.0 for value in record.target_position):
        raise ValueError("missing dynamic-solid target must resolve to zero position")


def _write_mappack_versioned(pack: MapPackV2, out_path: str, version: int) -> None:
    if len(pack.rooms) > 64:
        raise ValueError(f"too many rooms: {len(pack.rooms)}")
    if version not in (VERSION_V2, VERSION_V3, VERSION_V4, VERSION_V5):
        raise ValueError(f"unsupported map-pack version: {version}")
    if version < VERSION_V4 and pack.hazards:
        raise ValueError("hazard records require map-pack v4")
    if len(pack.hazards) > 512:
        raise ValueError(f"too many hazard records: {len(pack.hazards)}")
    if version < VERSION_V5 and pack.dynamic_solids:
        raise ValueError("dynamic-solid records require map-pack v5")
    if version == VERSION_V5:
        if len(pack.dynamic_solids) > 256:
            raise ValueError("too many dynamic-solid records (cap 256)")
        if (not pack.dsol_path.startswith("rom:/lvl/")
                or not pack.dsol_path.endswith(".dsol")
                or ".." in pack.dsol_path
                or len(pack.dsol_path.encode("utf-8")) >= 64):
            raise ValueError("MPP5 requires a DSOL path shorter than 64 bytes")
        if pack.dsol_size < 16:
            raise ValueError("MPP5 DSOL size is smaller than DSOL header")
        seen_sources = set()
        seen_solids = set()
        next_face = next_vertex = 0
        previous_source = -1
        for record in pack.dynamic_solids:
            _validate_dynamic_solid(record)
            if record.source_id <= previous_source:
                raise ValueError("dynamic-solid records must preserve source order")
            previous_source = record.source_id
            if record.source_id in seen_sources or record.solid_id in seen_solids:
                raise ValueError("duplicate dynamic-solid source or OG solid id")
            seen_sources.add(record.source_id)
            seen_solids.add(record.solid_id)
            if record.dsol_first_face != next_face or record.dsol_first_vertex != next_vertex:
                raise ValueError("DSOL actor ranges must be contiguous and source ordered")
            next_face += record.dsol_face_count
            next_vertex += record.dsol_vertex_count
    out = bytearray()
    out += {VERSION_V2: MAGIC_V2, VERSION_V3: MAGIC_V3,
            VERSION_V4: MAGIC_V4, VERSION_V5: MAGIC_V5}[version]
    out += struct.pack(">H", version)
    out += struct.pack(">H", pack.pipeline_version)
    out += _pack_fixed(pack.source_sha256, 64)
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
        if len(r.spawns) > 64:
            raise ValueError(f"room {r.id} has too many spawns: {len(r.spawns)}")
        for spawn in r.spawns:
            if spawn.kind not in (SPAWN_START, SPAWN_ANCHOR, SPAWN_ACTOR):
                raise ValueError(f"room {r.id} has invalid spawn kind: {spawn.kind}")
            _validate_spawn_flags(spawn)
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
            if version >= VERSION_V3:
                out += struct.pack(">B", s.actor_flags)
    if version >= VERSION_V4:
        for hazard in pack.hazards:
            _validate_hazard(hazard)
        out += struct.pack(">H", len(pack.hazards))
        for hazard in pack.hazards:
            out += struct.pack(">BI9f", hazard.kind, hazard.source_id,
                               *hazard.bounds_min, *hazard.bounds_max,
                               *hazard.spike_direction)
    if version == VERSION_V5:
        out += _pack_str(pack.dsol_path, 64)
        out += struct.pack(">IIH", pack.dsol_crc32, pack.dsol_size,
                           len(pack.dynamic_solids))
        for record in pack.dynamic_solids:
            out += struct.pack(
                ">IHBB3ffHHIIII I3f".replace(" ", ""),
                record.source_id, record.solid_id, record.kind, record.flags,
                *record.position, record.yaw_radians,
                record.cms_first_source_face_id, record.cms_face_count,
                record.dsol_first_face, record.dsol_face_count,
                record.dsol_first_vertex, record.dsol_vertex_count,
                record.target_source_id, *record.target_position)
    with open(out_path, "wb") as f:
        f.write(bytes(out))


def write_mappack_v2_binary(pack: MapPackV2, out_path: str) -> None:
    """Write the legacy v2 serialization (actor flags are not representable)."""
    _write_mappack_versioned(pack, out_path, VERSION_V2)


def write_mappack_v3_binary(pack: MapPackV2, out_path: str) -> None:
    """Write v3, appending one validated actor-flags byte per spawn."""
    _write_mappack_versioned(pack, out_path, VERSION_V3)


def write_mappack_v4_binary(pack: MapPackV2, out_path: str) -> None:
    """Write v4 with actor-backed DeathBlock/SpikeBlock volumes."""
    _write_mappack_versioned(pack, out_path, VERSION_V4)


def write_mappack_v5_binary(pack: MapPackV2, out_path: str) -> None:
    """Write v5 with hashed DSOL geometry and typed dynamic-solid records."""
    _write_mappack_versioned(pack, out_path, VERSION_V5)


def _read_mappack_versioned(path: str, expected_version: int) -> MapPackV2:
    with open(path, "rb") as f:
        data = f.read()
    off = 0

    def take(size: int) -> bytes:
        nonlocal off
        if size < 0 or off + size > len(data):
            raise ValueError("truncated map-pack binary")
        result = data[off:off + size]
        off += size
        return result

    def unpack(fmt: str):
        return struct.unpack(fmt, take(struct.calcsize(fmt)))

    magic_expected = {VERSION_V2: MAGIC_V2, VERSION_V3: MAGIC_V3,
                      VERSION_V4: MAGIC_V4, VERSION_V5: MAGIC_V5}[expected_version]
    magic = data[off:off + 4]; off += 4
    if len(magic) != 4 or magic != magic_expected:
        raise ValueError(f"bad mappack v{expected_version} magic: {magic!r}")
    (version,) = unpack(">H")
    if version != expected_version:
        raise ValueError(f"mappack version {version} != {expected_version}")
    (pipeline_version,) = unpack(">H")

    def read_str(n):
        return take(n).rstrip(b"\x00").decode("utf-8")

    source_sha256 = read_str(64)
    (scale,) = unpack(">f")
    (chunk_size,) = unpack(">f")
    global_colmesh_path = read_str(64)
    (g_crc,) = unpack(">I")
    (g_size,) = unpack(">I")
    (g_verts,) = unpack(">I")
    (g_tris,) = unpack(">I")
    (g_bvh,) = unpack(">I")
    start_room_id = read_str(16)
    (room_count,) = unpack(">H")
    if room_count > 64:
        raise ValueError(f"too many map-pack rooms: {room_count}")

    rooms: List[V2Room] = []
    for _ in range(room_count):
        rid = read_str(16)
        lvl_path = read_str(64)
        ro = unpack(">fff")
        amin = unpack(">fff")
        amax = unpack(">fff")
        (lvl_crc,) = unpack(">I")
        (lvl_size,) = unpack(">I")
        neighbors = {}
        for axis in NEIGHBOR_AXES:
            neighbors[axis] = read_str(16)
        (spawn_count,) = unpack(">H")
        if spawn_count > 64:
            raise ValueError(f"room {rid} has too many spawns: {spawn_count}")
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
            (kind,) = unpack(">B")
            if kind not in (SPAWN_START, SPAWN_ANCHOR, SPAWN_ACTOR):
                raise ValueError(f"invalid spawn kind: {kind}")
            (source_id,) = unpack(">I")
            room_id = read_str(16)
            pos = unpack(">fff")
            name = read_str(32)
            classname = read_str(32)
            (actor_flags,) = unpack(">B") if expected_version >= VERSION_V3 else (0,)
            spawn = V2Spawn(kind, source_id, room_id, tuple(pos), name, classname,
                            actor_flags)
            _validate_spawn_flags(spawn)
            r.spawns[i] = spawn

    hazards: List[V4Hazard] = []
    if expected_version >= VERSION_V4:
        (hazard_count,) = unpack(">H")
        if hazard_count > 512:
            raise ValueError(f"too many hazards: {hazard_count}")
        for _ in range(hazard_count):
            values = unpack(">BI9f")
            hazard = V4Hazard(
                kind=values[0], source_id=values[1],
                bounds_min=tuple(values[2:5]),
                bounds_max=tuple(values[5:8]),
                spike_direction=tuple(values[8:11]),
            )
            _validate_hazard(hazard)
            hazards.append(hazard)

    dsol_path = ""
    dsol_crc32 = dsol_size = 0
    dynamic_solids: List[V5DynamicSolid] = []
    if expected_version == VERSION_V5:
        dsol_path = read_str(64)
        (dsol_crc32, dsol_size, dynamic_count) = unpack(">IIH")
        if (not dsol_path.startswith("rom:/lvl/")
                or not dsol_path.endswith(".dsol") or ".." in dsol_path
                or dynamic_count > 256 or dsol_size < 16):
            raise ValueError("invalid MPP5 DSOL metadata")
        seen_sources = set()
        seen_solids = set()
        next_face = next_vertex = 0
        previous_source = -1
        for _ in range(dynamic_count):
            row = unpack(">IHBB3ffHHIIII I3f".replace(" ", ""))
            record = V5DynamicSolid(
                source_id=row[0], solid_id=row[1], kind=row[2], flags=row[3],
                position=tuple(row[4:7]), yaw_radians=row[7],
                cms_first_source_face_id=row[8], cms_face_count=row[9],
                dsol_first_face=row[10], dsol_face_count=row[11],
                dsol_first_vertex=row[12], dsol_vertex_count=row[13],
                target_source_id=row[14], target_position=tuple(row[15:18]))
            _validate_dynamic_solid(record)
            if record.source_id <= previous_source:
                raise ValueError("MPP5 dynamic solids are not in source order")
            previous_source = record.source_id
            if record.source_id in seen_sources or record.solid_id in seen_solids:
                raise ValueError("duplicate MPP5 dynamic-solid identity")
            seen_sources.add(record.source_id)
            seen_solids.add(record.solid_id)
            if record.dsol_first_face != next_face or record.dsol_first_vertex != next_vertex:
                raise ValueError("MPP5 DSOL ranges are not contiguous")
            next_face += record.dsol_face_count
            next_vertex += record.dsol_vertex_count
            dynamic_solids.append(record)

    if off != len(data):
        raise ValueError(f"trailing map-pack bytes: {len(data) - off}")

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
        hazards=hazards,
        dsol_path=dsol_path,
        dsol_crc32=dsol_crc32,
        dsol_size=dsol_size,
        dynamic_solids=dynamic_solids,
    )


def read_mappack_v2_binary(path: str) -> MapPackV2:
    """Read legacy v2; normalized actor flags default to zero."""
    return _read_mappack_versioned(path, VERSION_V2)


def read_mappack_v3_binary(path: str) -> MapPackV2:
    """Read v3 including strict actor-flag and record-bound validation."""
    return _read_mappack_versioned(path, VERSION_V3)


def read_mappack_v4_binary(path: str) -> MapPackV2:
    """Read v4 with validated actor-backed hazard volumes."""
    return _read_mappack_versioned(path, VERSION_V4)


def read_mappack_v5_binary(path: str) -> MapPackV2:
    """Read MPP5 and validate typed dynamic-solid ranges."""
    return _read_mappack_versioned(path, VERSION_V5)


def read_mappack_binary(path: str) -> MapPackV2:
    """Read either supported runtime version, preserving normalized flags."""
    with open(path, "rb") as f:
        magic = f.read(4)
    if magic == MAGIC_V2:
        return read_mappack_v2_binary(path)
    if magic == MAGIC_V3:
        return read_mappack_v3_binary(path)
    if magic == MAGIC_V4:
        return read_mappack_v4_binary(path)
    if magic == MAGIC_V5:
        return read_mappack_v5_binary(path)
    raise ValueError(f"bad map-pack magic: {magic!r}")
