#!/usr/bin/env python3
"""Validate player T3DM sidecar paths against animmap and manifest."""

from __future__ import annotations

import json
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def animation_paths(path: Path) -> dict[str, str]:
    data = path.read_bytes()
    assert data[:4] == b"T3M\x04"
    chunk_count = struct.unpack_from(">I", data, 4)[0]
    string_table = struct.unpack_from(">I", data, 24)[0]
    result: dict[str, str] = {}
    for index in range(chunk_count):
        entry = 44 + index * 4
        kind = chr(data[entry])
        offset = int.from_bytes(data[entry + 1:entry + 4], "big")
        if kind != "A":
            continue
        name_offset = struct.unpack_from(">I", data, offset)[0]
        path_offset = struct.unpack_from(">I", data, offset + 16)[0]
        name_start = string_table + name_offset
        path_start = string_table + path_offset
        name_end = data.find(b"\0", name_start)
        path_end = data.find(b"\0", path_start)
        assert name_end >= 0 and path_end >= 0
        name = data[name_start:name_end].decode()
        stream_path = data[path_start:path_end].decode()
        assert name not in result, f"duplicate clip {name}"
        assert stream_path.startswith("rom:/mdl/")
        assert not Path(stream_path).is_absolute()
        assert stream_path.endswith(".sdata")
        result[name] = stream_path
    assert result, "no animation chunks"
    return result


def main() -> int:
    manifest = json.loads((ROOT / "assets/og_converted/asset_manifest.json").read_text())
    player = next(asset for asset in manifest["assets"] if asset["id"] == "player")
    model_paths = animation_paths(ROOT / "assets/og_converted/models/player.t3dm")
    animmap_rows = (ROOT / "assets/og_converted/models/player.animmap").read_text().splitlines()
    assert animmap_rows[0].split("\t")[:2] == ["ANIMMAP", "1"]
    declared = int(animmap_rows[0].split("\t")[2])
    assert declared == len(animmap_rows) - 1
    animmap = {}
    for row in animmap_rows[1:]:
        fields = row.split("\t")
        assert len(fields) == 3
        name, stream_path, loop = fields
        assert name not in animmap and stream_path not in animmap.values()
        assert stream_path.startswith("rom:/mdl/") and loop in {"loop", "once"}
        animmap[name] = stream_path
    assert animmap == {name: model_paths[name] for name in animmap}
    required = {item["source"] for item in player["animations"]}
    assert required <= model_paths.keys()
    packaged = {output["package_path"] for output in player["outputs"]}
    for stream_path in animmap.values():
        package_path = "filesystem/" + stream_path.removeprefix("rom:/")
        assert package_path in packaged
        assert (ROOT / package_path).is_file()
    print(f"t3dm_stream_path_smoke: PASS ({len(model_paths)} chunks, {len(animmap)} runtime clips)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
