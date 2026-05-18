#!/usr/bin/env python3
"""Dump structural summary of a .colmesh file.

Usage:
    python3 tools/colmesh_dump.py <file.colmesh>
"""

import sys
import struct
from pathlib import Path

MAGIC    = b"CMSH"
FLAG_BVH = 0x0001

def read_i16_3(f):  return struct.unpack(">hhh", f.read(6))
def read_u16(f):    return struct.unpack(">H",   f.read(2))[0]
def read_u32(f):    return struct.unpack(">I",   f.read(4))[0]
def read_f32(f):    return struct.unpack(">f",   f.read(4))[0]
def read_f32_3(f):  return struct.unpack(">fff", f.read(12))

def dump(path: str):
    data = Path(path).read_bytes()
    f = open(path, "rb")

    magic = f.read(4)
    if magic != MAGIC:
        print(f"ERROR: bad magic {magic!r} (expected b'CMSH')", file=sys.stderr)
        return 1

    version = read_u16(f)
    flags   = read_u16(f)
    aabb_min = read_i16_3(f)
    aabb_max = read_i16_3(f)
    quant_scale  = read_f32(f)
    quant_origin = read_f32_3(f)

    vertex_count       = read_u32(f)
    triangle_count     = read_u32(f)
    bvh_node_count     = read_u32(f)
    surface_link_count = read_u32(f)

    vertex_offset   = read_u32(f)
    triangle_offset = read_u32(f)
    bvh_offset      = read_u32(f)
    surface_offset  = read_u32(f)

    file_size = Path(path).stat().st_size

    print(f"=== {path} ===")
    print(f"  magic:           {'OK (CMSH)' if magic==MAGIC else 'BAD'}")
    print(f"  version:         {version}")
    print(f"  flags:           0x{flags:04x}  {'has_bvh' if flags&FLAG_BVH else ''}")
    print(f"  aabb_min (quant):{aabb_min}")
    print(f"  aabb_max (quant):{aabb_max}")
    print(f"  quant_scale:     {quant_scale:.6f}")
    print(f"  quant_origin:    {quant_origin}")
    print(f"  vertex_count:    {vertex_count}")
    print(f"  triangle_count:  {triangle_count}")
    print(f"  bvh_node_count:  {bvh_node_count}")
    print(f"  link_count:      {surface_link_count}")
    print(f"  vertex_offset:   0x{vertex_offset:x}")
    print(f"  triangle_offset: 0x{triangle_offset:x}")
    print(f"  bvh_offset:      0x{bvh_offset:x}")
    print(f"  surface_offset:  0x{surface_offset:x}")
    print(f"  file size:       {file_size} bytes  ({file_size//1024} KB)")
    print(f"  budget 256 KB:   {'OK' if file_size <= 256*1024 else 'EXCEEDS BUDGET'}")

    # Spot-check: read first triangle
    if triangle_count > 0:
        f.seek(triangle_offset)
        i0, i1, i2, mat, face_id = struct.unpack(">HHHHH", f.read(10))
        f.read(2)  # pad
        print(f"  tri[0]:          i=({i0},{i1},{i2}) mat=0x{mat:04x} face_id={face_id}")

    # BVH root node
    if bvh_node_count > 0:
        f.seek(bvh_offset)
        mn = struct.unpack(">hhh", f.read(6))
        mx = struct.unpack(">hhh", f.read(6))
        lof, coz = struct.unpack(">HH", f.read(4))
        kind = "leaf" if coz > 0 else "internal"
        print(f"  bvh root ({kind}): aabb_min={mn} aabb_max={mx}")
        print(f"             left_or_first={lof} count_or_zero={coz}")

    f.close()

    # Validate offsets are 8-byte aligned
    for name, off in [("vertex",vertex_offset),("triangle",triangle_offset),
                      ("bvh",bvh_offset),("surface",surface_offset)]:
        if off % 8 != 0:
            print(f"  WARNING: {name}_offset {off} is not 8-byte aligned")

    return 0

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <file.colmesh>", file=sys.stderr)
        sys.exit(1)
    sys.exit(dump(sys.argv[1]))
