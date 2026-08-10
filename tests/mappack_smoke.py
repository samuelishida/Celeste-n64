#!/usr/bin/env python3
"""Map-pack manifest smoke test.

Asserts the map-pack manifest round-trips (JSON + binary), the start room
resolves, and the adjacency graph is symmetric.

Run:
    python3 tests/mappack_smoke.py
"""

import sys
import subprocess
import json
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tools"))

from mappack_format import (
    MapPack, read_mappack_json, read_mappack_binary,
)


def bake_pack(out_dir: str, map_path: str, chunk_size=1200.0, mappack_id="forsyken-city"):
    subprocess.run(
        [
            sys.executable, str(REPO / "tools" / "bake_map_pack.py"),
            map_path,
            "--out-dir", out_dir,
            "--chunk-size", str(chunk_size),
            "--scale", "0.2",
            "--mappack-id", mappack_id,
        ],
        check=True,
        capture_output=True,
    )


def test_json_roundtrip():
    out = "/tmp/madeline-mappack-smoke"
    bake_pack(out, str(REPO / "assets" / "og_converted" / "maps" / "1.map"))
    p = read_mappack_json(f"{out}/forsyken-city.mappack.json")
    assert p.id == "forsyken-city"
    assert len(p.rooms) > 1, "expected multiple rooms"
    assert p.start_room_id, "start room must resolve"
    start = [r for r in p.rooms if r.id == p.start_room_id][0]
    assert start.has_start_spawn, "start room must have the Start PlayerSpawn"
    # Atmosphere is shared.
    assert p.atmosphere.get("skybox") == "city"
    assert p.atmosphere.get("music") == "mus_lvl1"
    print(f"PASS: JSON roundtrip — {len(p.rooms)} rooms, start={p.start_room_id}")


def test_binary_roundtrip():
    out = "/tmp/madeline-mappack-smoke"
    p_json = read_mappack_json(f"{out}/forsyken-city.mappack.json")
    p_bin = read_mappack_binary(f"{out}/forsyken-city.mappack")
    assert len(p_bin.rooms) == len(p_json.rooms)
    assert p_bin.start_room_id == p_json.start_room_id
    assert abs(p_bin.scale - p_json.scale) < 1e-5
    assert abs(p_bin.chunk_size - p_json.chunk_size) < 1e-5
    assert p_bin.atmosphere["skybox"] == p_json.atmosphere["skybox"]
    assert p_bin.atmosphere["music"] == p_json.atmosphere["music"]
    for rj, rb in zip(p_json.rooms, p_bin.rooms):
        assert rj.id == rb.id, f"id mismatch {rj.id} vs {rb.id}"
        assert rj.lvl_path == rb.lvl_path
        assert rj.has_colmesh == rb.has_colmesh
    print(f"PASS: binary roundtrip — {len(p_bin.rooms)} rooms match JSON")


def test_adjacency_symmetric():
    out = "/tmp/madeline-mappack-smoke"
    p = read_mappack_json(f"{out}/forsyken-city.mappack.json")
    by_id = {r.id: r for r in p.rooms}
    for r in p.rooms:
        for axis, nb_id in r.neighbors.items():
            if not nb_id:
                continue
            assert nb_id in by_id, f"{r.id} neighbor {nb_id} not in rooms"
            nb = by_id[nb_id]
            # +X <-> -X, +Z <-> -Z
            opposite = {"+X": "-X", "-X": "+X", "+Z": "-Z", "-Z": "+Z"}[axis]
            assert nb.neighbors.get(opposite, "") == r.id, (
                f"adjacency not symmetric: {r.id}.{axis}={nb_id} but "
                f"{nb_id}.{opposite}={nb.neighbors.get(opposite,'')}"
            )
    print("PASS: adjacency graph symmetric")


def test_dfs_paths():
    """Room paths must match the filesystem/lvl/<pack>/ layout (dfs-path-prefix)."""
    out = "/tmp/madeline-mappack-smoke"
    p = read_mappack_json(f"{out}/forsyken-city.mappack.json")
    for r in p.rooms:
        assert r.lvl_path.startswith("rom:/lvl/forsyken-city/"), (
            f"lvl_path {r.lvl_path} does not match DFS layout"
        )
        assert r.lvl_path.endswith(".lvl")
        if r.has_colmesh:
            assert r.colmesh_path.startswith("rom:/lvl/forsyken-city/")
            assert r.colmesh_path.endswith(".colmesh")
    print("PASS: DFS paths match rom:/lvl/forsaken-city/ layout")


def main():
    test_json_roundtrip()
    test_binary_roundtrip()
    test_adjacency_symmetric()
    test_dfs_paths()
    print("\nAll mappack_smoke tests passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())