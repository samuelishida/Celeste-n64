#!/usr/bin/env python3
"""MPP3 actor-flag round-trip and malformed-input contract."""

import sys
import tempfile
from types import SimpleNamespace
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tools"))

from mappack_format import (
    MapPackV2, V2Room, V2Spawn, write_mappack_v2_binary,
    write_mappack_v3_binary, read_mappack_v2_binary,
    read_mappack_v3_binary, SPAWN_ACTOR,
)


def make_pack(actor_flags=0, classname="Refill"):
    spawn = V2Spawn(
        SPAWN_ACTOR, 17, "cell_00_00", (1.0, 2.0, 3.0), "refill", classname,
        actor_flags,
    )
    room = V2Room(
        "cell_00_00", "rom:/lvl/test/cell_00_00.lvl", (0.0, 0.0, 0.0),
        (0.0, 0.0, 0.0), (100.0, 100.0, 100.0), 0, 0,
        {"+X": "", "-X": "", "+Z": "", "-Z": ""}, [spawn],
    )
    return MapPackV2(
        "test", "a" * 64, 2, 0.2, 1000.0,
        "rom:/lvl/test/test.colmesh", 0, 0, 0, 0, 0,
        "cell_00_00", [room],
    )


def test_v3_round_trip_and_v2_default():
    with tempfile.TemporaryDirectory(prefix="mappack-v3-") as tmp:
        path = Path(tmp) / "flags.mappack"
        pack = make_pack(actor_flags=1)
        write_mappack_v3_binary(pack, str(path))
        loaded = read_mappack_v3_binary(str(path))
        assert loaded.rooms[0].spawns[0].actor_flags == 1

        write_mappack_v2_binary(pack, str(path))
        legacy = read_mappack_v2_binary(str(path))
        assert legacy.rooms[0].spawns[0].actor_flags == 0


def test_v3_rejects_bad_flags_and_truncation():
    with tempfile.TemporaryDirectory(prefix="mappack-v3-invalid-") as tmp:
        path = Path(tmp) / "flags.mappack"
        for flags, classname in ((2, "Refill"), (1, "PlayerSpawn")):
            try:
                write_mappack_v3_binary(make_pack(flags, classname), str(path))
                raise AssertionError("writer accepted invalid actor flags")
            except ValueError:
                pass

        write_mappack_v3_binary(make_pack(actor_flags=1), str(path))
        data = bytearray(path.read_bytes())
        data[-1] = 0x80
        path.write_bytes(data)
        try:
            read_mappack_v3_binary(str(path))
            raise AssertionError("reader accepted unknown actor flag")
        except ValueError:
            pass

        write_mappack_v3_binary(make_pack(actor_flags=1), str(path))
        path.write_bytes(path.read_bytes()[:-1])
        try:
            read_mappack_v3_binary(str(path))
            raise AssertionError("reader accepted truncated spawn record")
        except ValueError:
            pass


def test_refill_property_encoding():
    from bake_interconnected_map import BakeError, _actor_flags_for_spawn

    def spawn(value):
        return SimpleNamespace(
            classname="Refill", source_id=741,
            properties={} if value is None else {"double": value},
        )

    assert _actor_flags_for_spawn(spawn(None)) == 0
    assert _actor_flags_for_spawn(spawn("")) == 0
    assert _actor_flags_for_spawn(spawn("0")) == 0
    assert _actor_flags_for_spawn(spawn("1")) == 1
    assert _actor_flags_for_spawn(spawn("0.5")) == 1
    try:
        _actor_flags_for_spawn(spawn("not-a-number"))
        raise AssertionError("invalid refill property should fail bake")
    except BakeError as exc:
        assert "741" in str(exc)


if __name__ == "__main__":
    test_v3_round_trip_and_v2_default()
    test_v3_rejects_bad_flags_and_truncation()
    test_refill_property_encoding()
    print("PASS: mappack_v3_test")
