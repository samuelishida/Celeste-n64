#!/usr/bin/env python3
"""Check that the baked room only references TMEM-safe 3D materials."""

from pathlib import Path
import struct


MANIFEST = Path("filesystem/lvl/1-1.manifest")
TEXTURES = Path("assets/og_converted/textures")
BYTES_PER_RGBA16_PIXEL = 2
TMEM_BYTES = 4096


def read_sprite_size(path: Path) -> tuple[int, int]:
    data = path.read_bytes()
    assert data[:4] != b"DCA5", f"{path} must be uncompressed for direct budget inspection"
    width, height = struct.unpack(">HH", data[:4])
    return width, height


def main() -> None:
    materials = [line.strip() for line in MANIFEST.read_text().splitlines() if line.strip()]
    assert materials, "level manifest is empty"

    for material in materials:
        sprite_path = TEXTURES / f"{material}.sprite"
        assert sprite_path.exists(), f"missing sprite for material {material}"
        width, height = read_sprite_size(sprite_path)
        byte_count = width * height * BYTES_PER_RGBA16_PIXEL
        assert byte_count <= TMEM_BYTES, (
            f"{material} is {width}x{height} RGBA16 ({byte_count} bytes), "
            f"which exceeds {TMEM_BYTES}-byte TMEM"
        )
        print(f"PASS: {material} {width}x{height} RGBA16 uses {byte_count} bytes")

    print("material budget smoke test: PASS")


if __name__ == "__main__":
    main()
