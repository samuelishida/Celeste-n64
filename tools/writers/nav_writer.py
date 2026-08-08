#!/usr/bin/env python3
"""NAV writer — generates NAV from ParsedMap.

Generates NAV sidecar for TrafficBlock entities with waypoint data.

This is extracted from bake_nav.py to be a shared writer module.
"""

import sys
import struct
from pathlib import Path
from typing import List, Tuple

# Import library
sys.path.insert(0, str(Path(__file__).parent.parent))
from ogmap_lib import ParsedMap, classify_entity, EntityClass, transform_point


class NavStats:
    """Statistics from NAV generation."""
    def __init__(self):
        self.platforms: int = 0


def write_nav(
    parsed_map: ParsedMap,
    out_path: str,
    scale: float = 0.2
) -> NavStats:
    """Write NAV file from ParsedMap.

    Args:
        parsed_map: ParsedMap to convert
        out_path: Output .nav file path
        scale: World scale factor (not used for NAV, kept for API consistency)

    Returns:
        NavStats with generation statistics
    """
    stats = NavStats()

    # Find TrafficBlock entities and build entity index lookup
    traffic_entities = []
    for idx, ent in enumerate(parsed_map.entities):
        cd = classify_entity(ent)
        if cd and cd.entity_class == EntityClass.TRAFFIC_BLOCK:
            traffic_entities.append((idx, ent))

    # Build Node index by targetname for waypoint lookup
    nodes_by_target: dict = {}
    for ent in parsed_map.entities:
        if ent.classname == "Node":
            targetname = ent.properties.get("targetname", "")
            if targetname:
                nodes_by_target[targetname] = ent.origin

    platforms = []
    for ent_index, ent in traffic_entities:
        # Transform origin to game space (Y-up)
        game_origin = transform_point(ent.origin, scale)

        # Build waypoints from target connections
        target = ent.properties.get("target", "")
        if target and target in nodes_by_target:
            dest = nodes_by_target[target]
            game_dest = transform_point(dest, scale)
            waypoints = [game_origin, game_dest]
        else:
            # Fallback: simple offset from origin in game space
            waypoints = [
                game_origin,
                (game_origin[0] + 20.0, game_origin[1], game_origin[2])
            ]
        travel_time = 2.0
        platforms.append((ent_index, waypoints, travel_time))

    stats.platforms = len(platforms)

    # Write NAV file
    with open(out_path, 'wb') as f:
        # Magic
        f.write(b'NAV1')

        # Platform count
        f.write(struct.pack('<H', len(platforms)))

        # Write each platform
        for ent_index, waypoints, travel_time in platforms:
            # Entity index (deterministic: actual index in entities list)
            f.write(struct.pack('<H', ent_index))

            # Waypoint count
            f.write(struct.pack('<H', len(waypoints)))

            # Travel time
            f.write(struct.pack('<f', travel_time))

            # Waypoints
            for wp in waypoints:
                f.write(struct.pack('<fff', wp[0], wp[1], wp[2]))

    return stats
