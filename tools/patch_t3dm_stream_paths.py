#!/usr/bin/env python3
"""Rewrite tiny3d animation sidecar paths to canonical ROM paths."""

from __future__ import annotations

import struct
import sys
from pathlib import Path


def patch_t3dm_stream_paths(path: Path, prefix: str = "rom:/mdl/") -> list[tuple[str, str]]:
    data = bytearray(path.read_bytes())
    if data[:4] != b"T3M\x04":
        raise ValueError(f"{path}: unsupported T3DM header")
    chunk_count = struct.unpack_from(">I", data, 4)[0]
    string_table = struct.unpack_from(">I", data, 24)[0]
    changes: list[tuple[str, str]] = []
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
        name_end = data.find(0, name_start)
        path_end = data.find(0, path_start)
        if name_end < 0 or path_end < 0:
            raise ValueError(f"{path}: unterminated animation string")
        name = bytes(data[name_start:name_end]).decode("utf-8")
        old_path = bytes(data[path_start:path_end]).decode("utf-8")
        stream_name = Path(old_path).name
        if not stream_name.endswith(".sdata"):
            raise ValueError(f"{path}: animation {name} has non-sidecar path {old_path}")
        new_path = prefix + stream_name
        if len(new_path) > path_end - path_start:
            raise ValueError(f"{path}: canonical path longer than string slot for {name}")
        data[path_start:path_end] = new_path.encode("utf-8") + b"\0" * (path_end - path_start - len(new_path))
        changes.append((name, new_path))
    path.write_bytes(data)
    return changes


def main(argv: list[str]) -> int:
    if len(argv) != 1:
        print(f"usage: {Path(sys.argv[0]).name} MODEL.t3dm", file=sys.stderr)
        return 2
    try:
        for name, path in patch_t3dm_stream_paths(Path(argv[0])):
            print(f"{name}\t{path}")
        return 0
    except (OSError, ValueError) as exc:
        print(f"t3dm-stream-paths: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
