#!/usr/bin/env python3
"""Nav baker — TrafficBlock path data (.nav).

Extracts TrafficBlock → Node path waypoints from the OG map and writes
a binary .nav sidecar for future moving-platform runtime support.
"""

import sys
import struct
import io
import argparse
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))

from ogmap_lib import parse_map, transform_point

NAV_MAGIC = b"NAV1"


def extract_paths(parsed_map, scale):
    """Find TrafficBlock → Node links and return path data.

    Returns list of dicts: {entity_index, waypoints, travel_time}.
    """
    # Build Node index by targetname
    nodes_by_target: dict[str, tuple[float,float,float]] = {}
    for ent in parsed_map.entities:
        if ent.classname != "Node":
            continue
        tn = ent.properties.get("targetname", "")
        if tn:
            nodes_by_target[tn] = ent.origin

    paths = []
    for ei, ent in enumerate(parsed_map.entities):
        if ent.classname != "TrafficBlock":
            continue

        target = ent.properties.get("target", "")
        if not target or target not in nodes_by_target:
            print(f"[nav] TrafficBlock {ei}: no paired Node for target='{target}'")
            paths.append({"entity_index": ei, "waypoints": [], "travel_time": 2.0})
            continue

        node_pos = nodes_by_target[target]

        # Compute brush AABB center from all face points
        brush_center = ent.origin
        if ent.brushes:
            all_pts = []
            for brush in ent.brushes:
                for face in brush.faces:
                    all_pts.extend([face["p1"], face["p2"], face["p3"]])
            if all_pts:
                xs = [p[0] for p in all_pts]
                ys = [p[1] for p in all_pts]
                zs = [p[2] for p in all_pts]
                brush_center = (
                    (min(xs) + max(xs)) / 2,
                    (min(ys) + max(ys)) / 2,
                    (min(zs) + max(zs)) / 2,
                )

        travel_time = 2.0
        if "wait" in ent.properties:
            try:
                travel_time = float(ent.properties["wait"])
            except ValueError:
                pass

        brush_gs = transform_point(brush_center, scale)
        node_gs = transform_point(node_pos, scale)

        paths.append({
            "entity_index": ei,
            "waypoints": [brush_gs, node_gs],
            "travel_time": travel_time,
        })

    return paths


def write_nav(paths, out_path):
    """Write binary .nav file (little-endian for host convenience)."""
    count = len(paths)

    buf = io.BytesIO()
    buf.write(NAV_MAGIC)
    buf.write(struct.pack("<H", count))

    for p in paths:
        wpts = p["waypoints"]
        buf.write(struct.pack("<HHf", p["entity_index"], len(wpts), p["travel_time"]))
        for w in wpts:
            buf.write(struct.pack("<fff", w[0], w[1], w[2]))

    with open(out_path, "wb") as f:
        f.write(buf.getvalue())

    total_wpts = sum(len(p["waypoints"]) for p in paths)
    print(f"[nav] {count} platforms, {total_wpts} waypoints")
    print(f"[nav] wrote {out_path}")


def main():
    parser = argparse.ArgumentParser(description="Nav baker (.nav)")
    parser.add_argument("in_map", help="Input OG .map file")
    parser.add_argument("--out", required=True, help="Output .nav file")
    parser.add_argument("--scale", type=float, default=0.15, help="World scale")
    args = parser.parse_args()

    parsed_map = parse_map(args.in_map)
    paths = extract_paths(parsed_map, args.scale)
    write_nav(paths, args.out)


if __name__ == "__main__":
    main()
