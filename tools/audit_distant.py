#!/usr/bin/env python3
"""Audit the on-disk distant-LOD artifacts (Inc 1 baseline).

Decodes the published `filesystem/lvl/<pack>/*_distant.lvl` files (reusing
`tools/lvl_format.py`) and prints a per-cell table — faces, verts, file bytes
— plus totals. This is a measurement-only tool: it reports the quantities the
plan's budgets and byte math depend on (faces/verts/bytes). Material-run
counts (1,015 adjacent / 303 material-sorted) are computed by the C++
`CoalesceBatches` and are NOT reported here.

Usage:
    python3 tools/audit_distant.py [--pack forsyken-city]
"""

import argparse
import glob
import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))

from lvl_format import LvlFile


def audit_pack(pack_dir: str) -> int:
    """Decode every `*_distant.lvl` under `pack_dir` and print a table.

    Returns 0 on success, 1 if the directory is missing.
    """
    if not os.path.isdir(pack_dir):
        print(f"ERROR: distant pack dir not found: {pack_dir}", file=sys.stderr)
        return 1

    pattern = os.path.join(pack_dir, "*_distant.lvl")
    paths = sorted(glob.glob(pattern))
    if not paths:
        print(f"WARN: no *_distant.lvl files under {pack_dir}", file=sys.stderr)

    rows = []
    total_faces = 0
    total_verts = 0
    total_bytes = 0
    corrupt = 0

    for p in paths:
        name = os.path.basename(p)
        size = os.path.getsize(p)
        try:
            lvl = LvlFile.read(p)
            faces = len(lvl.faces)
            verts = len(lvl.vertices)
        except Exception as e:  # noqa: BLE001 - corrupt file must not abort the audit
            print(f"WARN: corrupt {name}: {e}", file=sys.stderr)
            corrupt += 1
            faces = -1
            verts = -1
        rows.append((name, faces, verts, size))
        if faces >= 0:
            total_faces += faces
            total_verts += verts
        total_bytes += size

    # Per-cell table.
    print(f"{'cell':<24} {'faces':>7} {'verts':>7} {'bytes':>9}")
    print("-" * 50)
    for name, faces, verts, size in rows:
        f = "ERR" if faces < 0 else str(faces)
        v = "ERR" if verts < 0 else str(verts)
        print(f"{name:<24} {f:>7} {v:>7} {size:>9}")

    print("-" * 50)
    print(f"files: {len(rows)}  (corrupt: {corrupt})")
    print(f"total_faces: {total_faces}")
    print(f"total_verts: {total_verts}")
    print(f"total_bytes: {total_bytes}")

    if rows:
        fs = [r[1] for r in rows if r[1] >= 0]
        vs = [r[2] for r in rows if r[2] >= 0]
        if fs:
            print(f"faces  min/avg/max: {min(fs)} / {sum(fs)/len(fs):.1f} / {max(fs)}")
        if vs:
            print(f"verts  min/avg/max: {min(vs)} / {sum(vs)/len(vs):.1f} / {max(vs)}")
        print(f"bytes  avg/cell: {total_bytes/len(rows):.1f}")

    return 0


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pack", default="forsyken-city",
                        help="map-pack id / filesystem subdir (default: forsyken-city)")
    args = parser.parse_args(argv)

    pack_dir = os.path.join(
        os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
        "filesystem", "lvl", args.pack,
    )
    return audit_pack(pack_dir)


if __name__ == "__main__":
    sys.exit(main())
