#!/usr/bin/env python3
"""Validate exported-WAV audio boundary and deterministic event metadata."""

from __future__ import annotations

import json
import os
import shutil
import math
import struct
import subprocess
import tempfile
import wave
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def main() -> int:
    manifest = json.loads((ROOT / "assets/og_converted/asset_manifest.json").read_text())
    audio = [asset for asset in manifest["assets"] if asset.get("kind") == "audio"]
    assert {asset["id"] for asset in audio} == {"player_dash_audio", "forsaken_city_theme_audio"}
    for asset in audio:
        assert asset["status"] == "planned" and not asset["package"]
        event = asset["audio_event"]
        assert event["channel_class"] in {"sfx", "music"}
        assert 0.0 <= event["volume"] <= 1.0
        assert isinstance(event["loop"], bool)
        assert not (ROOT / asset["source"]).exists(), "external export unexpectedly became implicit source"

    with tempfile.TemporaryDirectory(prefix="audio-manifest-smoke-") as temp:
        output = Path(temp) / "events.tsv"
        subprocess.run([
            "python3", "tools/generate_audio_events.py",
            "--manifest", "assets/og_converted/asset_manifest.json",
            "--out", str(output),
        ], cwd=ROOT, check=True, capture_output=True, text=True)
        assert output.read_text() == "AUDIOEVENTS\t1\t0\n"

        # Conversion fixture exercises audioconv64 when toolchain is present,
        # but remains host-safe on machines without N64 tools.
        audioconv = Path(os.environ.get("N64_INST", os.path.expanduser("~/.n64-bootstrap/opt/libdragon"))) / "bin" / "audioconv64"
        if audioconv.is_file() or shutil.which("audioconv64"):
            wav_path = Path(temp) / "fixture.wav"
            with wave.open(str(wav_path), "wb") as wav:
                wav.setnchannels(1)
                wav.setsampwidth(2)
                wav.setframerate(44100)
                wav.writeframes(b"".join(
                    struct.pack("<h", int(12000 * math.sin(2 * math.pi * 440 * sample / 44100)))
                    for sample in range(4096)
                ))
            subprocess.run([
                str(audioconv) if audioconv.is_file() else "audioconv64",
                "--wav-compress", "1", "-o", str(temp), str(wav_path),
            ], check=True, capture_output=True, text=True)
            wav64 = Path(temp) / "fixture.wav64"
            assert wav64.is_file() and wav64.stat().st_size > 32
    print("audio_manifest_smoke: PASS (planned external exports + WAV64 fixture)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
