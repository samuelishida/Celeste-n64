#!/usr/bin/env python3
"""Generate the versioned 1-1 baseline under tests/fixtures/baseline/.

Increment 0 of the OG → N64 pipeline freezes the *current* pipeline outputs as
a reproducible reference. This script decodes the baked artifacts
(manifest, LVL2, colmesh, NAV, T3DM) into deterministic structural summaries
and writes them next to a `baseline.json` registry carrying the map hash,
scale, format versions and artifact counts.

No test reads these summaries on the fly to "capture" expectations — they are
committed reference data that later parity tests compare against.

Run from the repo root:
    python3 tools/bake_baseline.py
"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
TOOLS = REPO / "tools"
LVL_DIR = REPO / "filesystem" / "lvl"
MAP = REPO / "assets" / "og_converted" / "maps" / "1-1.map"
OUT = REPO / "tests" / "fixtures" / "baseline"

SCALE = 0.2

sys.path.insert(0, str(TOOLS))

from lvl_format import LvlFile  # noqa: E402


# ── Decoders ─────────────────────────────────────────────────────────

def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def decode_lvl(path: Path) -> dict:
    lvl = LvlFile.read(str(path))
    mats = [s for s in lvl.strings if s not in {
        lvl.skybox_str_id, lvl.music_str_id, lvl.ambience_str_id}]
    # Recompute material list from manifest sidecar to match runtime reading.
    manifest = path.with_suffix(".manifest")
    if manifest.exists():
        mats = [ln.strip() for ln in manifest.read_text().splitlines() if ln.strip()]
    entities = []
    for e in lvl.entities:
        name = lvl.strings[e.classname_id] if e.classname_id < len(lvl.strings) else "?"
        entities.append({"id": e.classname_id, "name": name, "pos": list(e.position)})
    return {
        "magic": "LVL2",
        "version": 2,
        "collider_count": len(lvl.colliders),
        "face_count": len(lvl.faces),
        "vertex_count": len(lvl.vertices),
        "entity_count": len(lvl.entities),
        "string_count": len(lvl.strings),
        "materials": mats,
        "skybox": lvl.strings[lvl.skybox_str_id] if lvl.skybox_str_id < len(lvl.strings) else None,
        "music": lvl.strings[lvl.music_str_id] if lvl.music_str_id < len(lvl.strings) else None,
        "ambience": lvl.strings[lvl.ambience_str_id] if lvl.ambience_str_id < len(lvl.strings) else None,
        "snow_amount_q8": lvl.snow_amount_q8,
        "snow_dir": list(lvl.snow_dir),
        "entities": entities,
        "sha256": sha256(path),
    }


def decode_colmesh(path: Path) -> dict:
    data = path.read_bytes()
    (version, flags) = struct.unpack_from(">HH", data, 4)
    aabb_min = struct.unpack_from(">hhh", data, 8)
    aabb_max = struct.unpack_from(">hhh", data, 14)
    quant_scale = struct.unpack_from(">f", data, 20)[0]
    quant_origin = struct.unpack_from(">fff", data, 24)
    vc, tc, bnc, lc = struct.unpack_from(">IIII", data, 36)
    vo, to, bo, so = struct.unpack_from(">IIII", data, 52)
    def world_min(i): return quant_origin[i] + aabb_min[i] * quant_scale
    def world_max(i): return quant_origin[i] + aabb_max[i] * quant_scale
    return {
        "magic": "CMSH",
        "version": version,
        "flags": flags,
        "aabb_min_quant": list(aabb_min),
        "aabb_max_quant": list(aabb_max),
        "world_aabb_min": [round(world_min(i), 4) for i in range(3)],
        "world_aabb_max": [round(world_max(i), 4) for i in range(3)],
        "quant_scale": round(quant_scale, 6),
        "quant_origin": [round(x, 4) for x in quant_origin],
        "vertex_count": vc,
        "triangle_count": tc,
        "bvh_node_count": bnc,
        "surface_link_count": lc,
        "vertex_offset": vo,
        "triangle_offset": to,
        "bvh_offset": bo,
        "surface_offset": so,
        "sha256": sha256(path),
    }


def decode_nav(path: Path) -> dict:
    data = path.read_bytes()
    magic = data[:4].decode("ascii")
    count = struct.unpack_from("<H", data, 4)[0]
    platforms = []
    off = 6
    for i in range(count):
        ei, wpc, tt = struct.unpack_from("<HHf", data, off)
        off += 8
        wpts = []
        for _ in range(wpc):
            wpts.append([round(x, 4) for x in struct.unpack_from("<fff", data, off)])
            off += 12
        platforms.append({"entity_index": ei, "waypoint_count": wpc,
                          "travel_time": tt, "waypoints": wpts})
    return {
        "magic": magic,
        "platform_count": count,
        "platforms": platforms,
        "sha256": sha256(path),
    }


def decode_t3dm(path: Path) -> dict:
    """Summarize the offline T3DM artifact (structure only).

    T3DM layout (see tiny3d-main/docs/modelFormat.md):
      0x00  char[3]  magic "T3M" + u8 version
      0x04  u32      chunk count
      0x08  u16      total vertex count
      0x0A  u16      total index count
      0x0C  u32[3]   chunk type indices (vertex/indices/material)
      0x18  u32      string table offset
      0x1C  u32      block (runtime)
      0x20  s16[3]   AABB min
      0x26  s16[3]   AABB max
      0x2C  ChunkPointer[chunk_count]
    ChunkPointer: char type + u24 offset (byte offset from file start).
    """
    data = path.read_bytes()
    magic = data[:3].decode("ascii")
    version = data[3]
    chunk_count = struct.unpack_from(">I", data, 0x04)[0]
    total_vertex_count = struct.unpack_from(">H", data, 0x08)[0]
    total_index_count = struct.unpack_from(">H", data, 0x0A)[0]
    first_vertex_chunk = struct.unpack_from(">I", data, 0x0C)[0]
    first_indices_chunk = struct.unpack_from(">I", data, 0x10)[0]
    first_material_chunk = struct.unpack_from(">I", data, 0x14)[0]
    string_table_offset = struct.unpack_from(">I", data, 0x18)[0]
    aabb_min = struct.unpack_from(">hhh", data, 0x20)
    aabb_max = struct.unpack_from(">hhh", data, 0x26)
    chunk_types = []
    chunk_offsets = []
    table_off = 0x2C
    for i in range(chunk_count):
        if table_off + 4 > len(data):
            break
        ctype = chr(data[table_off])
        coff = (data[table_off + 1] << 16) | (data[table_off + 2] << 8) | data[table_off + 3]
        chunk_types.append(ctype)
        chunk_offsets.append(coff)
        table_off += 4
    return {
        "magic": magic,
        "version": version,
        "chunk_count": chunk_count,
        "chunk_types": chunk_types,
        "chunk_offsets": chunk_offsets,
        "total_vertex_count": total_vertex_count,
        "total_index_count": total_index_count,
        "first_vertex_chunk": first_vertex_chunk,
        "first_indices_chunk": first_indices_chunk,
        "first_material_chunk": first_material_chunk,
        "string_table_offset": string_table_offset,
        "aabb_min": list(aabb_min),
        "aabb_max": list(aabb_max),
        "offline_only": True,
        "note": "T3DM is produced/validated offline; renderer cutover is out of scope.",
        "sha256": sha256(path),
    }


# ── Baseline writer ──────────────────────────────────────────────────

def build_baseline(scale: float, map_sha: str, lvl, colmesh, nav, t3dm) -> dict:
    return {
        "map": "assets/og_converted/maps/1-1.map",
        "map_sha256": map_sha,
        "scale": scale,
        "versions": {
            "lvl": lvl["version"],
            "colmesh": colmesh["version"],
            "nav": nav.get("magic"),
            "t3dm": t3dm.get("version"),
        },
        "counts": {
            "lvl_colliders": lvl["collider_count"],
            "lvl_faces": lvl["face_count"],
            "lvl_vertices": lvl["vertex_count"],
            "lvl_entities": lvl["entity_count"],
            "colmesh_vertices": colmesh["vertex_count"],
            "colmesh_triangles": colmesh["triangle_count"],
            "colmesh_bvh_nodes": colmesh["bvh_node_count"],
            "nav_platforms": nav["platform_count"],
            "t3dm_chunks": t3dm["chunk_count"],
        },
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--scale", type=float, default=SCALE,
                        help="world scale used by the current pipeline (default 0.2)")
    parser.add_argument("--lvl-dir", type=Path, default=LVL_DIR,
                        help="directory containing baked 1-1 artifacts")
    parser.add_argument("--map", type=Path, default=MAP,
                        help="source OG .map (for SHA)")
    parser.add_argument("--out", type=Path, default=OUT,
                        help="baseline output directory (default tests/fixtures/baseline)")
    args = parser.parse_args()

    map_sha = sha256(args.map)
    lvl_path = args.lvl_dir / "1-1.lvl"
    colmesh_path = args.lvl_dir / "1-1.colmesh"
    t3dm_path = args.lvl_dir / "1-1.t3dm"

    if not lvl_path.exists() or not colmesh_path.exists() or not t3dm_path.exists():
        print(f"ERROR: missing baked artifacts in {args.lvl_dir}", file=sys.stderr)
        return 1

    # NAV is an offline sidecar not produced by the Makefile; generate it.
    with tempfile.TemporaryDirectory() as tmp:
        nav_path = Path(tmp) / "1-1.nav"
        subprocess.run([sys.executable, str(TOOLS / "bake_nav.py"),
                        str(args.map), "--out", str(nav_path),
                        "--scale", str(args.scale)],
                       check=True, capture_output=True)

        lvl = decode_lvl(lvl_path)
        colmesh = decode_colmesh(colmesh_path)
        nav = decode_nav(nav_path)
        t3dm = decode_t3dm(t3dm_path)

        room_out = args.out / "1-1"
        room_out.mkdir(parents=True, exist_ok=True)

        (room_out / "1-1.manifest").write_bytes(lvl_path.with_suffix(".manifest").read_bytes())
        (room_out / "1-1.lvl.summary.json").write_text(
            json.dumps(lvl, indent=2, sort_keys=True) + "\n")
        (room_out / "1-1.colmesh.summary.json").write_text(
            json.dumps(colmesh, indent=2, sort_keys=True) + "\n")
        (room_out / "1-1.nav.summary.json").write_text(
            json.dumps(nav, indent=2, sort_keys=True) + "\n")
        (room_out / "1-1.t3dm.summary.json").write_text(
            json.dumps(t3dm, indent=2, sort_keys=True) + "\n")
        (room_out / "1-1.t3dm").write_bytes(t3dm_path.read_bytes())

        registry = build_baseline(args.scale, map_sha, lvl, colmesh, nav, t3dm)
        (args.out / "baseline.json").write_text(
            json.dumps(registry, indent=2, sort_keys=True) + "\n")

        print(f"baseline written under {args.out}")
        print(f"  map_sha256={map_sha}")
        print(f"  lvl: {lvl['face_count']} faces / {lvl['vertex_count']} verts / {lvl['entity_count']} entities")
        print(f"  colmesh: {colmesh['triangle_count']} tris / {colmesh['vertex_count']} verts / {colmesh['bvh_node_count']} BVH")
        print(f"  nav: {nav['platform_count']} platforms")
        print(f"  t3dm: {t3dm['chunk_count']} chunks (offline)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
