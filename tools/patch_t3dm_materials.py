#!/usr/bin/env python3
"""Patch a .t3dm file so dynTextureCb fires for --ignore-materials models.

gltf_to_t3d --ignore-materials writes texReference=0 for both texture slots.
The runtime skips dynTextureCb when texReference==0 (t3dmodel.c:152).
This patcher sets texReference=1 so the callback fires, enabling per-material
primColor and sprite-texture loading.
"""

import struct
import sys
from pathlib import Path


# Material chunk binary layout (after 6-byte chunk header: uint16 type, uint32 size):
#   offset 0:  colorCombiner    uint64  8
#   offset 8:  otherModeValue   uint64  8
#   offset 16: otherModeMask    uint64  8
#   offset 24: blendMode        uint32  4
#   offset 28: drawFlags        uint32  4
#   offset 32: _unused00_       uint8   1
#   offset 33: fogMode          uint8   1
#   offset 34: setColorFlags    uint8   1
#   offset 35: vertexFxFunc     uint8   1
#   offset 36: primColor[4]     uint8   4
#   offset 40: envColor[4]      uint8   4
#   offset 44: blendColor[4]    uint8   4
#   offset 48: name             uint32  4  (string-table offset)
#   offset 52: texReferenceA    uint32  4  ← PATCH to non-zero
#   ... texture A tile params (44 bytes total for tex A) ...
#   offset 96: texReferenceB    uint32  4  ← PATCH to non-zero
#   ... texture B tile params (44 bytes total for tex B) ...
# Total material chunk data: 140 bytes

OFF_TEX_REF_A = 52  # bytes from chunk data start
OFF_TEX_REF_B = 96

CHUNK_TYPE_MATERIAL = 1


def patch_t3dm(path_in: str, path_out: str, ref_value: int = 1) -> int:
    """Patch texReference fields in all material chunks. Returns count of patched fields."""
    data = bytearray(open(path_in, "rb").read())

    if data[:3] != b"T3M":
        print(f"ERROR: not a .t3dm file (magic={data[:3]!r})", file=sys.stderr)
        return 0

    patched = 0
    pos = 4  # skip magic + version byte

    while pos + 6 <= len(data):
        chunk_type = struct.unpack_from("<H", data, pos)[0]
        chunk_size = struct.unpack_from("<I", data, pos + 2)[0]

        if pos + 6 + chunk_size > len(data):
            break

        if chunk_type == CHUNK_TYPE_MATERIAL:
            chunk_data_start = pos + 6

            for off in (OFF_TEX_REF_A, OFF_TEX_REF_B):
                field_off = chunk_data_start + off
                if field_off + 4 <= chunk_data_start + chunk_size:
                    current = struct.unpack_from("<I", data, field_off)[0]
                    if current == 0:
                        struct.pack_into("<I", data, field_off, ref_value)
                        patched += 1

        pos += 6 + chunk_size

    if patched > 0:
        Path(path_out).write_bytes(data)
        print(f"[patch] {Path(path_in).name}: {patched} texReference fields set to {ref_value}")
    else:
        print(f"[patch] {Path(path_in).name}: no zero texReference fields found (already patched?)")

    return patched


def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <input.t3dm> [output.t3dm]", file=sys.stderr)
        sys.exit(1)

    path_in = sys.argv[1]
    path_out = sys.argv[2] if len(sys.argv) > 2 else path_in
    patch_t3dm(path_in, path_out)


if __name__ == "__main__":
    main()
