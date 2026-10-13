#!/usr/bin/env python3
"""Compare baked hazard volumes with OG Map.LoadActor/WorldBounds rules."""

import math
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tools"))

from ogmap_lib import parse_map, transform_point
from ogmap_lib.brush_geom import compute_face_polygon
from ogworld.parse import build_world_ir


def bounds(points):
    return tuple(min(p[i] for p in points) for i in range(3)), tuple(
        max(p[i] for p in points) for i in range(3)
    )


def test_hazard_actor_bounds_match_source():
    map_path = REPO / "assets/og_converted/maps/1.map"
    parsed = parse_map(str(map_path))
    build = build_world_ir(str(map_path), scale=0.2, strict=True)
    expected_entities = {
        i: entity for i, entity in enumerate(parsed.entities)
        if entity.classname in ("DeathBlock", "SpikeBlock")
    }
    actual = {hazard.source_id: hazard for hazard in build.hazards}
    assert set(actual) == set(expected_entities)
    assert len(build.hazards) > 0

    for source_id, entity in expected_entities.items():
        hazard = actual[source_id]
        assert entity.brushes
        first_solid = entity.brushes[0]
        source_points = []
        for face_index in range(len(first_solid.faces)):
            source_points.extend(compute_face_polygon(first_solid.faces, face_index))
        base_points = [transform_point(point, 0.2) for point in source_points]
        base_min, base_max = bounds(base_points)
        center = tuple((base_min[i] + base_max[i]) * 0.5 for i in range(3))
        local_min = tuple(base_min[i] - center[i] for i in range(3))
        local_max = tuple(base_max[i] - center[i] for i in range(3))
        angle = math.radians(int(entity.properties.get("angle", "0")))
        cosine, sine = math.cos(angle), math.sin(angle)
        corners = []
        for x in (local_min[0], local_max[0]):
            for y in (local_min[1], local_max[1]):
                for z in (local_min[2], local_max[2]):
                    corners.append((
                        center[0] + cosine * x + sine * z,
                        center[1] + y,
                        center[2] - sine * x + cosine * z,
                    ))
        expected_min, expected_max = bounds(corners)
        for actual_value, expected_value in zip(
            (*hazard.bounds_min, *hazard.bounds_max),
            (*expected_min, *expected_max),
        ):
            assert abs(actual_value - expected_value) <= 1e-5
        assert all(lo <= hi for lo, hi in zip(hazard.bounds_min, hazard.bounds_max))

        if hazard.kind == "spike":
            direction = hazard.spike_direction
            size_x = local_max[0] - local_min[0]
            size_y = local_max[2] - local_min[2]
            size_z = local_max[1] - local_min[1]
            if size_z < size_x and size_z < size_y:
                expected_direction = (0.0, 1.0, 0.0)
            elif size_x < size_y:
                expected_direction = (-1.0, 0.0, 0.0)
            else:
                expected_direction = (0.0, 0.0, 1.0)
            expected_direction = (
                cosine * expected_direction[0] + sine * expected_direction[2],
                expected_direction[1],
                -sine * expected_direction[0] + cosine * expected_direction[2],
            )
            for actual_component, expected_component in zip(direction, expected_direction):
                assert abs(actual_component - expected_component) <= 1e-6
            assert abs(sum(v * v for v in direction) - 1.0) <= 1e-6
            assert sum(abs(v) > 1e-6 for v in direction) == 1
        else:
            assert hazard.kind == "death"


if __name__ == "__main__":
    test_hazard_actor_bounds_match_source()
    print("PASS: og_hazard_volume_smoke")
