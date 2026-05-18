#!/usr/bin/env python3
"""Generate a placeholder first-room.glb matching gameplay .map brush layout.

Produces a valid binary glTF 2.0 file with box geometry in runtime (Y-up)
coordinates.  No external dependencies — stdlib only.

Usage:
    python3 tools/gen_first_room_glb.py [out.glb]
"""

import struct
import json
import sys
from pathlib import Path

# ---------------------------------------------------------------------------
# Box definitions — run-time Y-up coords (port units)
# ---------------------------------------------------------------------------
# Each box: (xmin, ymin, zmin, xmax, ymax, zmax, material_name)
BOXES = [
    # brush 0 — spawn platform floor
    (-8.0,  0.0,   -4.0,   6.0,  0.64,  4.0,   "rock_1"),
    # brush 1 — spawn platform back wall (-Y face in runtime)
    (-8.0,  0.0,   -4.0,   6.0,  2.56, -3.36,  "rock_1"),
    # brush 2 — spawn platform front wall (+Y face)
    (-8.0,  0.0,    3.36,  6.0,  2.56,  4.0,   "rock_1"),
    # brush 3 — landing platform floor
    (12.0,  0.0,   -4.0,  18.0,  0.64,  4.0,   "rock_1"),
    # brush 4 — landing back wall
    (12.0,  0.0,   -4.0,  18.0,  2.56, -3.36,  "rock_1"),
    # brush 5 — landing front wall
    (12.0,  0.0,    3.36, 18.0,  2.56,  4.0,   "rock_1"),
    # brush 6 — landing left end wall (faces gap at x=12)
    (12.0,  0.0,   -4.0,  12.64, 2.56,  4.0,   "rock_1"),
    # brush 7 — climb wall (climbable)
    (18.0,  0.64, -0.32, 18.64, 8.0,   0.32,  "rock_1_climbable"),
    # brush 8 — kill-drop walkway
    (12.0,  0.0,    4.0,  18.0,  0.32,  4.8,   "rock_1"),
]

# Map material names → indices in glTF
MATERIAL_INDICES = {"rock_1": 0, "rock_1_climbable": 1}

# ---------------------------------------------------------------------------
# glTF 2.0 binary helpers
# ---------------------------------------------------------------------------

def box_mesh(box):
    """Return (positions_flat, normals_flat, indices, material_idx) for a box."""
    x0,y0,z0, x1,y1,z1, mat = box
    # 24 vertices: 4 per face × 6 faces
    # Each vertex: pos(3) + normal(3)
    # Face order: +X, -X, +Y, -Y, +Z, -Z
    f = [
        # +X face (normal 1,0,0)
        (x1,y0,z0, 1,0,0), (x1,y1,z0, 1,0,0), (x1,y1,z1, 1,0,0), (x1,y0,z1, 1,0,0),
        # -X face (normal -1,0,0)
        (x0,y0,z1, -1,0,0), (x0,y1,z1, -1,0,0), (x0,y1,z0, -1,0,0), (x0,y0,z0, -1,0,0),
        # +Y face (normal 0,1,0)
        (x0,y1,z0, 0,1,0), (x1,y1,z0, 0,1,0), (x1,y1,z1, 0,1,0), (x0,y1,z1, 0,1,0),
        # -Y face (normal 0,-1,0)
        (x0,y0,z1, 0,-1,0), (x1,y0,z1, 0,-1,0), (x1,y0,z0, 0,-1,0), (x0,y0,z0, 0,-1,0),
        # +Z face (normal 0,0,1)
        (x0,y0,z1, 0,0,1), (x1,y0,z1, 0,0,1), (x1,y1,z1, 0,0,1), (x0,y1,z1, 0,0,1),
        # -Z face (normal 0,0,-1)
        (x1,y0,z0, 0,0,-1), (x0,y0,z0, 0,0,-1), (x0,y1,z0, 0,0,-1), (x1,y1,z0, 0,0,-1),
    ]
    positions = []
    normals = []
    for v in f:
        positions.extend(v[:3])
        normals.extend(v[3:6])
    base_idx = len(positions) // 3 - 24  # offset for indices (in local vertex array)
    indices = []
    for fi in range(6):
        b = base_idx + fi * 4
        indices.extend([b, b+1, b+2, b, b+2, b+3])
    mat_idx = MATERIAL_INDICES.get(mat, 0)
    return positions + normals, indices, mat_idx

def pad4(n):
    return (n + 3) & ~3

def write_glb(output_path):
    all_positions = []
    all_indices = []
    all_materials = []
    max_index = 0
    index_acc = 0

    for box in BOXES:
        attrs, indices, mat = box_mesh(box)
        nv = len(attrs) // 6  # 3 pos + 3 normal
        all_positions.extend(attrs)
        for idx in indices:
            all_indices.append(idx + index_acc)
        all_materials.extend([mat] * (len(indices) // 3))
        index_acc += nv

    # Pack into float32 buffer
    pos_bytes = struct.pack(f'<{len(all_positions)}f', *all_positions)
    idx_count = len(all_indices)
    # Use uint16 if possible, uint32 otherwise
    if max(all_indices) < 65535:
        idx_bytes = struct.pack(f'<{idx_count}H', *all_indices)
        idx_component_type = 5123  # UNSIGNED_SHORT
    else:
        idx_bytes = struct.pack(f'<{idx_count}I', *all_indices)
        idx_component_type = 5125  # UNSIGNED_INT

    # Pad each buffer section to 4
    bin_data = pos_bytes
    pad_len = pad4(len(bin_data)) - len(bin_data)
    if pad_len:
        bin_data += b'\x00' * pad_len

    idx_offset = len(bin_data)
    bin_data += idx_bytes
    pad_len = pad4(len(bin_data)) - len(bin_data)
    if pad_len:
        bin_data += b'\x00' * pad_len

    total_verts = len(all_positions) // 6
    byte_stride = 24  # 6 floats × 4 bytes
    pos_byte_len = total_verts * byte_stride

    # Build glTF JSON
    gltf = {
        "asset": {"version": "2.0", "generator": "gen_first_room_glb.py"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"mesh": 0, "name": "first-room"}],
        "meshes": [{
            "name": "first-room-mesh",
            "primitives": [
                {
                    "attributes": {
                        "POSITION": 0,
                        "NORMAL": 1,
                    },
                    "indices": 2,
                    "material": 0,
                    "mode": 4,  # TRIANGLES
                }
            ]
        }],
        "materials": [
            {"name": "rock_1", "pbrMetallicRoughness": {"baseColorFactor": [0.4, 0.3, 0.2, 1.0]}},
            {"name": "rock_1_climbable", "pbrMetallicRoughness": {"baseColorFactor": [0.2, 0.5, 0.3, 1.0]}},
        ],
        "accessors": [
            {  # 0: positions
                "bufferView": 0,
                "componentType": 5126,  # FLOAT
                "count": total_verts,
                "type": "VEC3",
                "min": [0,0,0],
                "max": [1,1,1],
            },
            {  # 1: normals
                "bufferView": 1,
                "componentType": 5126,
                "count": total_verts,
                "type": "VEC3",
            },
            {  # 2: indices
                "bufferView": 2,
                "componentType": idx_component_type,
                "count": idx_count,
                "type": "SCALAR",
            },
        ],
        "bufferViews": [
            {  # 0: positions
                "buffer": 0,
                "byteOffset": 0,
                "byteLength": pos_byte_len,
                "byteStride": byte_stride,
                "target": 34962,  # ARRAY_BUFFER
            },
            {  # 1: normals
                "buffer": 0,
                "byteOffset": 12,  # 3 floats offset from position start
                "byteLength": pos_byte_len,
                "byteStride": byte_stride,
                "target": 34962,
            },
            {  # 2: indices
                "buffer": 0,
                "byteOffset": idx_offset,
                "byteLength": len(idx_bytes),
                "target": 34963,  # ELEMENT_ARRAY_BUFFER
            },
        ],
        "buffers": [
            {"byteLength": len(bin_data)}
        ],
    }

    # Fix accessor min/max
    pos_data = [all_positions[i*3:(i+1)*3] if i*3+2 < len(all_positions) else [0,0,0] for i in range(total_verts)]
    xs = [p[0] for p in pos_data]
    ys = [p[1] for p in pos_data]
    zs = [p[2] for p in pos_data]
    gltf["accessors"][0]["min"] = [min(xs), min(ys), min(zs)]
    gltf["accessors"][0]["max"] = [max(xs), max(ys), max(zs)]

    json_str = json.dumps(gltf, separators=(',', ':'))
    json_bytes = json_str.encode('utf-8')
    # Pad JSON to 4-byte boundary with spaces
    json_pad = pad4(len(json_bytes)) - len(json_bytes)
    if json_pad:
        json_bytes += b' ' * json_pad

    # Write .glb
    total_len = 12 + 8 + len(json_bytes) + 8 + len(bin_data)
    with open(output_path, 'wb') as f:
        f.write(struct.pack('<4sII', b'glTF', 2, total_len))
        # JSON chunk
        f.write(struct.pack('<I4s', len(json_bytes), b'JSON'))
        f.write(json_bytes)
        # BIN chunk
        f.write(struct.pack('<I4s', len(bin_data), b'BIN\x00'))
        f.write(bin_data)

    print(f"wrote {output_path} ({total_len} bytes, {len(BOXES)} boxes, {total_verts} verts, {idx_count} indices)")

if __name__ == '__main__':
    out = sys.argv[1] if len(sys.argv) > 1 else "assets/rooms/first-room/first-room.glb"
    write_glb(out)
