#!/usr/bin/env python3
"""MPP4 hazard-volume round-trip, validation, and legacy compatibility."""

import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tools"))

from mappack_format import (
    MapPackV4, V2Room, V4Hazard, write_mappack_v4_binary,
    read_mappack_v4_binary, write_mappack_v3_binary,
)


def make_pack(hazards):
    room = V2Room(
        "cell_00_00", "rom:/lvl/test/cell_00_00.lvl", (0.0, 0.0, 0.0),
        (0.0, 0.0, 0.0), (100.0, 100.0, 100.0), 0, 0,
        {"+X": "", "-X": "", "+Z": "", "-Z": ""}, [],
    )
    return MapPackV4(
        "test", "a" * 64, 2, 0.2, 1000.0,
        "rom:/lvl/test/test.colmesh", 0, 0, 0, 0, 0,
        "cell_00_00", [room], hazards,
    )


def test_round_trip_and_fail_closed_validation():
    hazards = [
        V4Hazard(1, 12, (-1.0, 2.0, 3.0), (4.0, 5.0, 6.0)),
        V4Hazard(2, 13, (-2.0, -1.0, 0.0), (2.0, 1.0, 4.0), (0.0, 0.0, 1.0)),
    ]
    with tempfile.TemporaryDirectory(prefix="mappack-v4-") as tmp:
        path = Path(tmp) / "hazards.mappack"
        pack = make_pack(hazards)
        write_mappack_v4_binary(pack, str(path))
        loaded = read_mappack_v4_binary(str(path))
        assert loaded.hazards == hazards
        assert path.read_bytes()[:6] == b"MPP4\x00\x04"

        try:
            write_mappack_v3_binary(pack, str(path))
            raise AssertionError("v3 writer silently dropped hazards")
        except ValueError:
            pass

        for invalid in (
            V4Hazard(1, 1, (2.0, 0.0, 0.0), (1.0, 1.0, 1.0)),
            V4Hazard(3, 1, (0.0, 0.0, 0.0), (1.0, 1.0, 1.0)),
            V4Hazard(2, 1, (0.0, 0.0, 0.0), (1.0, 1.0, 1.0), (2.0, 0.0, 0.0)),
        ):
            try:
                write_mappack_v4_binary(make_pack([invalid]), str(path))
                raise AssertionError("writer accepted malformed hazard")
            except ValueError:
                pass


if __name__ == "__main__":
    test_round_trip_and_fail_closed_validation()
    print("PASS: mappack_v4_test")
