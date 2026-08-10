#!/usr/bin/env python3
"""Inc 5 reachability test.

BFS the manifest's ±X/±Z graph from the named Start room and require every
collision-bearing room to be reachable. Decoration-only or isolated hazard
cells must be explicitly classified and listed, never silently counted as
playable world.

Run:
    python3 tests/interconnected_reachability.py
"""

import sys
import json
import subprocess
import tempfile
import shutil
from pathlib import Path
from collections import deque

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tools"))

MAIN_MAP = REPO / "assets" / "og_converted" / "maps" / "1.map"
CHUNK_SIZE = 1200.0
SCALE = 0.2


def bake(out_dir: str) -> dict:
    proc = subprocess.run(
        [
            sys.executable, str(REPO / "tools" / "bake_interconnected_map.py"),
            str(MAIN_MAP),
            "--out-dir", out_dir,
            "--chunk-size", str(int(CHUNK_SIZE)),
            "--scale", str(SCALE),
        ],
        capture_output=True, text=True,
    )
    if proc.returncode != 0:
        raise AssertionError(f"bake failed: {proc.stderr}\n{proc.stdout}")
    with open(Path(out_dir) / "interconnected_report.json") as f:
        return json.load(f)


def test_reachability():
    """Every room is reachable from the Start room via ±X/±Z adjacency."""
    d = tempfile.mkdtemp(prefix="inc5-reach-")
    try:
        report = bake(d)
        from mappack_format import read_mappack_v2_binary
        pack = read_mappack_v2_binary(str(Path(d) / "staging" / "forsyken-city.mappack"))
        by_id = {r.id: r for r in pack.rooms}
        start = pack.start_room_id
        assert start in by_id, f"start room {start} not in rooms"

        # BFS from start.
        seen = {start}
        dq = deque([start])
        while dq:
            cur = dq.popleft()
            for ax, nb in by_id[cur].neighbors.items():
                if nb and nb in by_id and nb not in seen:
                    seen.add(nb)
                    dq.append(nb)

        unreachable = [r for r in by_id if r not in seen]
        # Every room must be reachable (no isolated playable cells).
        assert not unreachable, f"unreachable rooms: {unreachable}"
        print(f"PASS: all {len(by_id)} rooms reachable from {start}")
    finally:
        shutil.rmtree(d, ignore_errors=True)


def test_start_room_has_start_spawn():
    """The Start room contains the named 'Start' spawn."""
    d = tempfile.mkdtemp(prefix="inc5-start-")
    try:
        bake(d)
        from mappack_format import read_mappack_v2_binary
        pack = read_mappack_v2_binary(str(Path(d) / "staging" / "forsyken-city.mappack"))
        start_room = next(r for r in pack.rooms if r.id == pack.start_room_id)
        starts = [s for s in start_room.spawns if s.kind == 0]
        assert len(starts) == 1, f"start room has {len(starts)} Start spawns"
        assert starts[0].name == "Start"
        print(f"PASS: start room {pack.start_room_id} has named Start spawn")
    finally:
        shutil.rmtree(d, ignore_errors=True)


def main() -> int:
    test_reachability()
    test_start_room_has_start_spawn()
    print("\nAll interconnected_reachability tests passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
