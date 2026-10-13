#!/usr/bin/env python3
"""Generate deterministic ROM audio event metadata from asset manifest."""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path

from asset_manifest import load_manifest, validate_manifest


def generate(manifest: dict) -> str:
    diagnostics = validate_manifest(manifest, check_outputs=False)
    if diagnostics:
        raise ValueError("manifest validation failed: " + "; ".join(diagnostics))
    events = []
    ids: set[str] = set()
    paths: set[str] = set()
    for asset in manifest.get("assets", []):
        if asset.get("kind") != "audio" or asset.get("status") != "active" or not asset.get("package"):
            continue
        metadata = asset.get("audio_event")
        outputs = asset.get("outputs") or []
        if not isinstance(metadata, dict) or len(outputs) != 1:
            raise ValueError(f"audio asset {asset.get('id')}: event metadata/output required")
        event_id = metadata.get("id")
        channel = metadata.get("channel_class")
        volume = metadata.get("volume")
        loop = metadata.get("loop")
        package_path = outputs[0].get("package_path")
        if (not isinstance(event_id, str) or not event_id or "\t" in event_id or "\n" in event_id or
                channel not in ("sfx", "music") or not isinstance(volume, (int, float)) or
                not math.isfinite(float(volume)) or not 0.0 <= float(volume) <= 1.0 or
                not isinstance(loop, bool) or not isinstance(package_path, str) or
                not package_path.startswith("filesystem/audio/")):
            raise ValueError(f"audio asset {asset.get('id')}: invalid event metadata")
        rom_path = "rom:/" + package_path[len("filesystem/"):]
        if event_id in ids or rom_path in paths:
            raise ValueError(f"duplicate audio event id/path: {event_id} {rom_path}")
        ids.add(event_id)
        paths.add(rom_path)
        events.append((event_id, rom_path, channel, f"{float(volume):.6f}", "1" if loop else "0"))
    events.sort(key=lambda row: row[0])
    lines = [f"AUDIOEVENTS\t1\t{len(events)}"]
    lines.extend("\t".join(row) for row in events)
    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", default="assets/og_converted/asset_manifest.json")
    parser.add_argument("--out", required=True)
    args = parser.parse_args()
    try:
        manifest = load_manifest(args.manifest)
        out = Path(args.out)
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(generate(manifest), encoding="utf-8")
        print(f"audio-events: wrote {out}")
        return 0
    except (OSError, ValueError) as exc:
        print(f"audio-events: {exc}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
