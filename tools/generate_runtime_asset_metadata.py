#!/usr/bin/env python3
"""Generate ROM-resident animation manifest from host asset registry."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", required=True)
    parser.add_argument("--asset-id", required=True)
    parser.add_argument("--out", required=True)
    args = parser.parse_args(argv)
    try:
        manifest = json.loads(Path(args.manifest).read_text(encoding="utf-8"))
        asset = next(a for a in manifest["assets"] if a.get("id") == args.asset_id)
        if asset.get("source_asset"):
            asset = next(a for a in manifest["assets"] if a.get("id") == asset["source_asset"])
        animations = asset.get("animations", [])
        model_output = next((item for item in asset.get("outputs", []) if item.get("format") == "t3dm"), None)
        if model_output is None:
            raise ValueError(f"{asset['id']}: animated source has no T3DM output")
        model_stem = Path(model_output["path"]).stem
        rows: list[str] = []
        for item in animations:
            if not isinstance(item, dict):
                raise ValueError(f"{args.asset_id}: animation entries must be objects")
            name = item["source"]
            loop = item["loop"]
            if loop not in ("loop", "once"):
                raise ValueError(f"{args.asset_id}: unsupported loop policy {loop}")
            # Stream filename is assigned by gltf_to_t3d in source clip order;
            # patch_t3dm_stream_paths canonicalizes it to this ROM prefix.
            stream_index = item.get("stream_index")
            if stream_index is None:
                raise ValueError(f"{args.asset_id}:{name}: stream_index required")
            stream = f"rom:/mdl/{model_stem}.{int(stream_index)}.sdata"
            if any("\t" in value or "\n" in value for value in (name, stream, loop)):
                raise ValueError(f"{args.asset_id}:{name}: tab/newline in metadata")
            rows.append(f"{name}\t{stream}\t{loop}")
        output = Path(args.out)
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(f"ANIMMAP\t1\t{len(rows)}\n" + "\n".join(rows) + "\n", encoding="utf-8")
        return 0
    except (OSError, KeyError, ValueError, json.JSONDecodeError) as exc:
        print(f"runtime-asset-metadata: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
