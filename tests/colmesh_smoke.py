#!/usr/bin/env python3
"""Structural smoke test for .colmesh files.

Run: python3 tests/colmesh_smoke.py <file.colmesh>
Exit 0 on pass, 1 on failure.
"""
import sys
import struct
from pathlib import Path

def check(path: str) -> bool:
    data = Path(path).read_bytes()
    if len(data) < 72:
        print(f"FAIL: file too small ({len(data)} bytes)", file=sys.stderr)
        return False

    import io
    f = io.BytesIO(data)

    # Magic + version
    magic = f.read(4)
    if magic != b"CMSH":
        print(f"FAIL: bad magic {magic!r}", file=sys.stderr)
        return False
    version = struct.unpack(">H", f.read(2))[0]
    if version != 1:
        print(f"FAIL: version {version} != 1", file=sys.stderr)
        return False

    flags = struct.unpack(">H", f.read(2))[0]
    aabb_min = struct.unpack(">hhh", f.read(6))
    aabb_max = struct.unpack(">hhh", f.read(6))
    quant_scale  = struct.unpack(">f", f.read(4))[0]
    quant_origin = struct.unpack(">fff", f.read(12))

    for field_name, val in [("quant_scale", quant_scale)]:
        if val <= 0:
            print(f"FAIL: {field_name} must be positive, got {val}", file=sys.stderr)
            return False

    vc, tc, bc, lc = struct.unpack(">IIII", f.read(16))
    voff, toff, boff, soff = struct.unpack(">IIII", f.read(16))

    if tc > 32767:
        print(f"FAIL: triangle_count {tc} > 32767", file=sys.stderr)
        return False
    if bc > 65535:
        print(f"FAIL: bvh_node_count {bc} > 65535", file=sys.stderr)
        return False

    # 8-byte alignment
    for name, off in [("vertex",voff),("triangle",toff),("bvh",boff),("surface",soff)]:
        if off % 8 != 0:
            print(f"FAIL: {name}_offset {off} not 8-byte aligned", file=sys.stderr)
            return False

    # Offsets within file
    for name, off in [("vertex",voff),("triangle",toff),("bvh",boff)]:
        if off >= len(data):
            print(f"FAIL: {name}_offset {off} >= file size {len(data)}", file=sys.stderr)
            return False

    # Spot-check first triangle vertex indices < vertex_count
    if tc > 0:
        f.seek(toff)
        i0, i1, i2 = struct.unpack(">HHH", f.read(6))
        for idx in [i0, i1, i2]:
            if idx >= vc:
                print(f"FAIL: tri[0] index {idx} >= vertex_count {vc}", file=sys.stderr)
                return False
        mat, face_id = struct.unpack(">HH", f.read(4))
        pad = struct.unpack(">H", f.read(2))[0]
        if pad != 0:
            print(f"FAIL: tri[0].pad is {pad}, must be 0", file=sys.stderr)
            return False
        if face_id != 0:
            print(f"FAIL: tri[0].face_id {face_id} != 0 (face_id == array index invariant)", file=sys.stderr)
            return False

    # Check BVH root AABB contains mesh AABB
    if bc > 0:
        f.seek(boff)
        bmn = struct.unpack(">hhh", f.read(6))
        bmx = struct.unpack(">hhh", f.read(6))
        for i in range(3):
            if bmn[i] > aabb_min[i] or bmx[i] < aabb_max[i]:
                # BVH root should fully contain mesh AABB
                # Note: this is a warning, not fatal — quantization rounding may differ slightly
                print(f"WARN: BVH root AABB may not contain mesh AABB on axis {i}")

    file_size = len(data)
    budget    = 256 * 1024
    print(f"PASS: {path}  [{tc} tris, {vc} verts, {bc} BVH nodes, {file_size} bytes]")
    if file_size > budget:
        print(f"  WARNING: exceeds 256 KB budget ({file_size}/{budget})")
    return True

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <file.colmesh>", file=sys.stderr)
        sys.exit(1)
    sys.exit(0 if check(sys.argv[1]) else 1)
