#!/usr/bin/env python3
"""Bake a .lvl file's face geometry into a .colmesh collision sidecar.

Usage:
    python3 tools/colmesh_bake.py <in.lvl> <out.colmesh>

The .colmesh format is documented in docs/colmesh_format.md and
src/user/gameplay/physics/coll_mesh.hpp (generated in Inc 3).

Binary layout: big-endian (N64 native, matches .lvl convention).

Material mapping (from .lvl material_id string name suffix):
    default                → 0x0001  solid
    *_oneway               → 0x0003  solid | oneway
    *_death / *_kill       → 0x0005  solid | death
    *_climbable            → 0x0009  solid | climbable
    *_ice                  → 0x0011  solid | ice
    trigger_*              → 0x0000  non-solid trigger (not yet wired)

BVH: top-down median split on longest AABB axis.  Depth limit 30 so the
     uint16 stack[32] in the runtime traversal is never exhausted.
"""

import sys
import struct
import math
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from lvl_format import LvlFile

# ---------------------------------------------------------------------------
# Material flag bits (must stay in sync with inc-4-notes.md and coll_mesh.hpp)
# ---------------------------------------------------------------------------
MAT_SOLID     = 0x0001
MAT_ONEWAY    = 0x0002
MAT_DEATH     = 0x0004
MAT_CLIMBABLE = 0x0008
MAT_ICE       = 0x0010

def material_flags(tex_name: str) -> int:
    t = tex_name.lower()
    if t.startswith("trigger_"):
        return 0
    flags = MAT_SOLID
    if t.endswith("_oneway"):
        flags |= MAT_ONEWAY
    if t.endswith("_death") or t.endswith("_kill"):
        flags |= MAT_DEATH
    if t.endswith("_climbable"):
        flags |= MAT_CLIMBABLE
    if t.endswith("_ice"):
        flags |= MAT_ICE
    return flags

# ---------------------------------------------------------------------------
# Triangulate
# ---------------------------------------------------------------------------

def triangulate_lvl(lvl, manifest_path: str = None) -> tuple:
    """Return (positions, triangles) where:
       positions: list of (x,y,z) float tuples
       triangles: list of (i0,i1,i2, material_uint16, face_id_uint16) tuples
    """
    # Material IDs in .lvl faces are remapped to manifest-order indices
    # by bake_map.py.  Read the .manifest sidecar to get the correct
    # name for each index; fall back to .lvl string table (legacy).
    material_names = []
    if manifest_path:
        try:
            with open(manifest_path, 'r') as mf:
                material_names = [l.strip() for l in mf if l.strip()]
        except FileNotFoundError:
            pass
    # Fallback: use .lvl string table (wrong when manifest remap happened)
    strings = material_names if material_names else lvl.strings
    positions = []
    pos_index = {}  # (x,y,z) tuple → int index (dedup by exact float bits)
    triangles = []

    def add_vertex(pos):
        key = (pos[0], pos[1], pos[2])
        if key not in pos_index:
            pos_index[key] = len(positions)
            positions.append(key)
        return pos_index[key]

    face_id = 0
    for face in lvl.faces:
        if face.vertex_count < 3:
            face_id += 1
            continue
        tex_name = strings[face.material_id] if face.material_id < len(strings) else ""
        mat = material_flags(tex_name)
        verts = [lvl.vertices[face.vertex_start + j].pos for j in range(face.vertex_count)]
        indices = [add_vertex(v) for v in verts]
        # Fan triangulation from index 0
        for k in range(1, face.vertex_count - 1):
            i0 = indices[0]
            i1 = indices[k]
            i2 = indices[k + 1]
            tri_face_id = len(triangles)  # face_id == triangle array index
            triangles.append((i0, i1, i2, mat, tri_face_id))
        face_id += 1

    return positions, triangles

# ---------------------------------------------------------------------------
# Quantization
# ---------------------------------------------------------------------------

INT16_MAX = 32767

def compute_quant(positions):
    if not positions:
        return (0.0, 0.0, 0.0), 1.0
    xs = [p[0] for p in positions]
    ys = [p[1] for p in positions]
    zs = [p[2] for p in positions]
    origin = (min(xs), min(ys), min(zs))
    max_range = max(
        max(xs) - origin[0],
        max(ys) - origin[1],
        max(zs) - origin[2],
        1e-6
    )
    scale = max_range / INT16_MAX
    return origin, scale

def quantize(pos, origin, scale):
    x = int(round((pos[0] - origin[0]) / scale))
    y = int(round((pos[1] - origin[1]) / scale))
    z = int(round((pos[2] - origin[2]) / scale))
    def clamp(v): return max(-INT16_MAX, min(INT16_MAX, v))
    return (clamp(x), clamp(y), clamp(z))

# ---------------------------------------------------------------------------
# BVH build — top-down median split
# ---------------------------------------------------------------------------

class BvhNode:
    __slots__ = ('aabb_min', 'aabb_max', 'left_or_first', 'count_or_zero')
    def __init__(self):
        self.aabb_min = (INT16_MAX, INT16_MAX, INT16_MAX)
        self.aabb_max = (-INT16_MAX, -INT16_MAX, -INT16_MAX)
        self.left_or_first = 0
        self.count_or_zero = 0

def tri_centroid(tri, qverts):
    a, b, c = qverts[tri[0]], qverts[tri[1]], qverts[tri[2]]
    return ((a[0]+b[0]+c[0])/3.0, (a[1]+b[1]+c[1])/3.0, (a[2]+b[2]+c[2])/3.0)

def tri_aabb(tri, qverts):
    pts = [qverts[tri[0]], qverts[tri[1]], qverts[tri[2]]]
    mn = (min(p[0] for p in pts), min(p[1] for p in pts), min(p[2] for p in pts))
    mx = (max(p[0] for p in pts), max(p[1] for p in pts), max(p[2] for p in pts))
    return mn, mx

def union_aabb(a_min, a_max, b_min, b_max):
    return (
        (min(a_min[0], b_min[0]), min(a_min[1], b_min[1]), min(a_min[2], b_min[2])),
        (max(a_max[0], b_max[0]), max(a_max[1], b_max[1]), max(a_max[2], b_max[2]))
    )

MAX_LEAF_TRIS = 4  # tunable — affects BVH depth and query speed
MAX_DEPTH = 30

def build_bvh(tris, qverts, depth=0, _flat=None) -> tuple:
    """Return (node_list, flat_sorted_tris) in depth-first order.

    flat_sorted_tris is a flat list of triangle tuples in leaf order.
    Leaf nodes reference contiguous ranges in flat_sorted_tris:
        left_or_first = start index in flat_sorted_tris
        count_or_zero = count

    The caller must rebuild the triangles[] array in flat_sorted_tris order
    (updating face_ids to their new positions) before writing the file.
    """
    top_call = (_flat is None)
    if top_call:
        _flat = []

    node = BvhNode()

    # Compute AABB for all triangles in this set
    mn = (INT16_MAX, INT16_MAX, INT16_MAX)
    mx = (-INT16_MAX, -INT16_MAX, -INT16_MAX)
    for tri in tris:
        t_mn, t_mx = tri_aabb(tri, qverts)
        mn, mx = union_aabb(mn, mx, t_mn, t_mx)
    node.aabb_min = mn
    node.aabb_max = mx

    def make_leaf(leaf_tris):
        first = len(_flat)
        _flat.extend(leaf_tris)
        node.left_or_first = first
        node.count_or_zero = len(leaf_tris)
        return [node]

    if len(tris) <= MAX_LEAF_TRIS or depth >= MAX_DEPTH:
        nodes = make_leaf(tris)
        return (nodes, _flat) if top_call else nodes

    # Split on longest axis by median centroid
    ext = (mx[0]-mn[0], mx[1]-mn[1], mx[2]-mn[2])
    axis = ext.index(max(ext))
    centroids = [(tri_centroid(tri, qverts)[axis], tri) for tri in tris]
    centroids.sort(key=lambda x: x[0])
    mid = len(centroids) // 2
    left_tris  = [c[1] for c in centroids[:mid]]
    right_tris = [c[1] for c in centroids[mid:]]

    if not left_tris or not right_tris:
        nodes = make_leaf(tris)
        return (nodes, _flat) if top_call else nodes

    left_nodes  = build_bvh(left_tris,  qverts, depth+1, _flat)
    right_nodes = build_bvh(right_tris, qverts, depth+1, _flat)

    # Internal node: left_or_first = offset to right child = len(left_nodes) + 1
    node.left_or_first = len(left_nodes) + 1
    node.count_or_zero = 0

    nodes = [node] + left_nodes + right_nodes
    return (nodes, _flat) if top_call else nodes

# ---------------------------------------------------------------------------
# .colmesh binary writer (big-endian)
# ---------------------------------------------------------------------------

MAGIC = b"CMSH"
VERSION = 1
FLAG_HAS_BVH = 0x0001

def write_colmesh(out_path: str, positions, triangles, bvh_nodes,
                  quant_origin, quant_scale, surface_links=None):
    if surface_links is None:
        surface_links = []

    qverts = [quantize(p, quant_origin, quant_scale) for p in positions]
    tri_count = len(triangles)
    vert_count = len(qverts)
    bvh_count  = len(bvh_nodes)
    link_count = len(surface_links)

    # Compute mesh AABB in quantized space
    if qverts:
        mesh_min = (min(v[0] for v in qverts), min(v[1] for v in qverts), min(v[2] for v in qverts))
        mesh_max = (max(v[0] for v in qverts), max(v[1] for v in qverts), max(v[2] for v in qverts))
    else:
        mesh_min = mesh_max = (0, 0, 0)

    # Header is 64 bytes, 8-byte aligned
    HEADER_SIZE = 64  # adjusted below to actual
    # CollHeader layout (big-endian):
    #   char[4] magic
    #   uint16 version, uint16 flags
    #   int16[3] aabb_min, int16[3] aabb_max   (12 bytes)
    #   float quant_scale                       (4 bytes)
    #   float[3] quant_origin                   (12 bytes)
    #   uint32 vertex_count, triangle_count, bvh_node_count, surface_link_count  (16 bytes)
    #   uint32 vertex_offset, triangle_offset, bvh_offset, surface_link_offset   (16 bytes)
    # Total header = 4+2+2+6+6+4+12+16+16 = 68 bytes → pad to 72 (8-byte aligned)
    HEADER_SIZE = 72

    # Sizes
    VERT_SIZE  = 6   # int16[3]
    TRI_SIZE   = 12  # uint16 i0,i1,i2, material, face_id, pad
    NODE_SIZE  = 16  # int16[6], uint16[2]
    LINK_SIZE  = 4   # uint16 face_id, owner_id

    def align8(x): return (x + 7) & ~7

    vertex_offset   = HEADER_SIZE
    triangle_offset = align8(vertex_offset   + vert_count  * VERT_SIZE)
    bvh_offset      = align8(triangle_offset + tri_count   * TRI_SIZE)
    surface_offset  = align8(bvh_offset      + bvh_count   * NODE_SIZE)

    flags = FLAG_HAS_BVH if bvh_count > 0 else 0

    with open(out_path, "wb") as f:
        # Header
        f.write(MAGIC)
        f.write(struct.pack(">HH", VERSION, flags))
        f.write(struct.pack(">hhh", *mesh_min))
        f.write(struct.pack(">hhh", *mesh_max))
        f.write(struct.pack(">f",   quant_scale))
        f.write(struct.pack(">fff", *quant_origin))
        f.write(struct.pack(">IIII", vert_count, tri_count, bvh_count, link_count))
        f.write(struct.pack(">IIII", vertex_offset, triangle_offset, bvh_offset, surface_offset))
        # Pad to 72 bytes (header is currently 4+2+2+6+6+4+12+16+16 = 68)
        pad = HEADER_SIZE - f.tell()
        assert pad >= 0, "header overflowed"
        f.write(b'\x00' * pad)

        # Vertices
        assert f.tell() == vertex_offset, f"vertex offset mismatch: {f.tell()} vs {vertex_offset}"
        for qv in qverts:
            f.write(struct.pack(">hhh", *qv))
        # Pad to 8-byte boundary
        while f.tell() % 8 != 0:
            f.write(b'\x00')

        # Triangles
        assert f.tell() == triangle_offset, f"tri offset mismatch: {f.tell()} vs {triangle_offset}"
        for tri in triangles:
            i0, i1, i2, mat, face_id = tri
            f.write(struct.pack(">HHHHHxx", i0, i1, i2, mat, face_id))  # xx = 2 pad bytes = 0
        while f.tell() % 8 != 0:
            f.write(b'\x00')

        # BVH nodes
        assert f.tell() == bvh_offset, f"bvh offset mismatch: {f.tell()} vs {bvh_offset}"
        for node in bvh_nodes:
            mn = node.aabb_min
            mx = node.aabb_max
            f.write(struct.pack(">hhhhhh", mn[0],mn[1],mn[2], mx[0],mx[1],mx[2]))
            f.write(struct.pack(">HH", node.left_or_first, node.count_or_zero))
        while f.tell() % 8 != 0:
            f.write(b'\x00')

        # Surface links (sorted by face_id — none for static geometry)
        assert f.tell() == surface_offset, f"link offset mismatch: {f.tell()} vs {surface_offset}"
        for (fid, oid) in sorted(surface_links, key=lambda x: x[0]):
            f.write(struct.pack(">HH", fid, oid))

    return f.tell() if False else surface_offset + link_count * LINK_SIZE

# ---------------------------------------------------------------------------
# main
# ---------------------------------------------------------------------------

def main():
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} <in.lvl> <out.colmesh>", file=sys.stderr)
        sys.exit(1)

    lvl_path  = sys.argv[1]
    out_path  = sys.argv[2]

    print(f"[colmesh_bake] reading {lvl_path}")
    lvl = LvlFile.read(lvl_path)

    print(f"[colmesh_bake] {len(lvl.faces)} faces, {len(lvl.vertices)} vertices")

    manifest_path = Path(out_path).with_suffix('.manifest')
    positions, triangles = triangulate_lvl(lvl, str(manifest_path))
    print(f"[colmesh_bake] triangulated → {len(triangles)} tris, {len(positions)} unique verts")

    if not triangles:
        print("[colmesh_bake] ERROR: no triangles", file=sys.stderr)
        sys.exit(1)

    assert len(triangles) <= 32767, \
        f"triangle_count {len(triangles)} exceeds uint16 BVH index limit 32767"

    quant_origin, quant_scale = compute_quant(positions)
    qverts = [quantize(p, quant_origin, quant_scale) for p in positions]
    print(f"[colmesh_bake] quant_scale={quant_scale:.6f}  origin={quant_origin}")

    print("[colmesh_bake] building BVH...")
    bvh, sorted_tris = build_bvh(list(triangles), qverts)
    print(f"[colmesh_bake] BVH: {len(bvh)} nodes  (max allowed 65535)")
    assert len(bvh) <= 65535, "BVH node count overflows uint16"

    # Rebuild triangles in leaf order so leaf left_or_first+count is contiguous.
    # Update face_id to equal new array position (face_id == array_index invariant).
    reordered_tris = []
    for new_idx, tri in enumerate(sorted_tris):
        i0, i1, i2, mat, _old_face_id = tri
        reordered_tris.append((i0, i1, i2, mat, new_idx))

    assert len(reordered_tris) == len(triangles), "sorted tri count mismatch"

    size = write_colmesh(out_path, positions, reordered_tris, bvh,
                         quant_origin, quant_scale)
    file_size = Path(out_path).stat().st_size
    print(f"[colmesh_bake] wrote {out_path}  ({file_size} bytes)")

    budget = 256 * 1024
    if file_size > budget:
        print(f"[colmesh_bake] WARNING: {file_size} bytes exceeds 256 KB budget ({budget} bytes)")
        print("[colmesh_bake] See data-model.md §10 for NO-GO fallback procedure")
    else:
        print(f"[colmesh_bake] size OK ({file_size}/{budget} bytes = {100*file_size//budget}% of budget)")

if __name__ == "__main__":
    main()
