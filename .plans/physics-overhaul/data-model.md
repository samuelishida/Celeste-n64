# Data model — `.colmesh` format + runtime structures

No relational DB. "Schema" here = on-disk file format + runtime structs +
build-time pipeline. Locking this before the increment DAG is the point.

## 1. Entities & relationships

```
Level (existing .t3dm render mesh)
  └── 1:1 ──► .colmesh sidecar
                ├── CollHeader
                ├── Vertex[]      (quantized int16 positions)
                ├── Triangle[]    (i0,i1,i2,material,face_id)
                ├── BvhNode[]     (linear, depth-first)
                └── SurfaceLink[] (face_id → MovingSurface owner_id, sparse)

Room (runtime, existing)
  └── owns CollMesh*  (replaces Collider[512])
  └── owns MovingSurface[]  (unchanged)
```

## 2. On-disk layout (binary, little-endian — N64 r4300 is BE but DFS
   converts; match existing `.t3dm` convention)

```
struct CollHeader {
  char     magic[4];        // "CMSH"
  uint16_t version;         // 1
  uint16_t flags;           // bit0 = has_bvh
  int16_t  aabb_min[3];     // quantized — see §2a for dequantization
  int16_t  aabb_max[3];
  float    quant_scale;     // see §2a
  float    quant_origin[3]; // see §2a
  uint32_t vertex_count;    // ≤ 65535 (uint16 indices in CollTriangle)
  uint32_t triangle_count;  // ≤ 32767 (so 2N-1 BVH nodes ≤ 65535 for uint16 BVH indices)
  uint32_t bvh_node_count;  // ≤ 65535
  uint32_t surface_link_count;
  uint32_t vertex_offset;   // bytes from header start; 8-B aligned
  uint32_t triangle_offset;
  uint32_t bvh_offset;
  uint32_t surface_link_offset;
};

struct CollVertex { int16_t pos[3]; };        // 6 B

// §2a — Dequantization formula (pin, all consumers):
//   world_pos.xyz = quant_origin.xyz + (int16_pos.xyz * quant_scale)
// CollHeader.aabb_min / aabb_max are stored in the SAME quantized space as
// CollVertex.pos — i.e. they are int16 pre-dequant. Same formula applies.
// CollBvhNode.aabb_min/max are also int16 pre-dequant, same formula.
// quant_origin is chosen at bake time so that all source positions land in
// [-32767, 32767] / quant_scale + quant_origin.

struct CollTriangle {                          // 12 B
  uint16_t i0, i1, i2;
  uint16_t material;   // bit0=solid bit1=oneway bit2=death bit3=climbable bit4=ice
  uint16_t face_id;    // == array index in CollTriangle[]. Stored redundantly
                       // for stability across format evolutions; bake tool
                       // asserts triangles[i].face_id == i for all i.
  uint16_t pad;        // MUST be 0 (bake tool zeros explicitly; checksum tests
                       // depend on this).
};

struct CollBvhNode {                           // 16 B
  int16_t  aabb_min[3];                        // quantized — see §2a
  int16_t  aabb_max[3];
  uint16_t left_or_first;   // internal: left child idx; leaf: first triangle idx
  uint16_t count_or_zero;   // 0 = internal node; else leaf triangle count
};
// BVH traversal pseudocode (canonical — pin to avoid re-derivation):
//   stack = [0]; while stack not empty:
//     idx = pop; node = nodes[idx];
//     if !ray_vs_aabb(node.aabb): continue
//     if node.count_or_zero > 0:        // leaf
//       for t in [node.left_or_first, node.left_or_first + count):
//         test triangles[t]
//     else:                              // internal
//       left  = idx + 1
//       right = idx + node.left_or_first     // "skip pointer" — right child
//                                            //   sits after left subtree
//       push right; push left
// Traversal stack: fixed `uint16_t stack[32]` on caller's call stack — depth
// limit 32 enforced at bake (BVH builder splits until depth ≤ 30).

struct CollSurfaceLink { uint16_t face_id; uint16_t owner_id; };
```

## 3. Constraints & indexes

- `magic == "CMSH"`, `version == 1` (reject otherwise at load).
- `vertex_offset`, `triangle_offset`, `bvh_offset`, `surface_link_offset` all
  8-byte aligned (rdram DMA-friendly).
- `triangle.i0|i1|i2 < vertex_count` (importer validates; runtime asserts in
  debug builds only).
- `face_id == array index` in `CollTriangle[]`. Material lookup is therefore
  `triangles[face_id]` direct. Bake tool asserts the invariant.
- Stable across rebakes — same source vert order → same id. Vertex dedup
  uses *exact bit-equal int16 quantized positions* (after quantization), not
  pre-quant float compare. Deterministic across machines/compilers.
- BVH is depth-first array; left child = `idx + 1`, right child = `idx +
  left_or_first` for internal nodes (skip-pointer form). Leaves have
  `count_or_zero > 0` and reference a contiguous `Triangle[]` slice.
- BVH node count ≤ `2 * triangle_count - 1` (binary tree upper bound).

## 4. Query patterns

The format and BVH layout are justified by these reads:

1. **Segment vs mesh (raycast)** — DFS BVH; per-leaf Möller–Trumbore against
   listed triangles. Used by `RaycastRoomSource` replacement, by spike/death
   queries, and by camera occlusion check.
2. **Swept sphere vs mesh** — broad-phase: BVH query with inflated query AABB
   = swept-sphere AABB; narrow-phase: sphere-vs-triangle (Ericson 5.2.7) per
   leaf tri. Used by `PlayerMotor::Step` core resolve.
3. **Overlap AABB → triangle list** — BVH query with point AABB. Used by
   ground-snap "what's under me?" check and by climb scan.
4. **Material lookup by face_id** — direct `triangles[face_id]` (face_id is
   index). Used after a hit to ask "is this oneway / death / climbable?".
5. **Surface ownership lookup by face_id** — binary search over sorted
   `SurfaceLink[]`. Sparse; only platform faces appear. Used to resolve
   moving-surface owner for stored-velocity carry. **Sentinel: returns
   `INVALID_OWNER = 0xFFFF` when face is static (not in `SurfaceLink[]`).**
   Material flags on a moving-platform face are still honoured (a climbable
   moving platform is both grabbable AND carries the player; Inc 6 checks
   material independently of `SurfaceOwnerOf`).

If a query is not in this list, the schema is wrong — return here.

## 5. Sample row (smallest plausible mesh)

3-triangle floor wedge, axis-aligned:

```
CollHeader: magic="CMSH" version=1 flags=1
  aabb_min=(-100,0,-100) aabb_max=(100,10,100) quant_scale=1.0
  vertex_count=5 triangle_count=3 bvh_node_count=1 surface_link_count=0
Vertices:  (-100,0,-100) (100,0,-100) (100,0,100) (-100,0,100) (0,10,0)
Triangles: (0,1,2 material=0x0001 face_id=0)
           (0,2,3 material=0x0001 face_id=1)
           (3,2,4 material=0x0009 face_id=2)  // 0x0009 = solid + climbable
BvhNodes:  [ aabb=full, left_or_first=0, count_or_zero=3 ]   // single leaf
SurfaceLinks: (none)
```

## 6. Build pipeline (importer side)

- Source: same glTF the renderer consumes.
- Selection rule: triangles whose source mesh has node name prefix
  `coll_` are baked into `.colmesh`; everything else is render-only. Name
  prefix `coll_render_` means a mesh contributes to both.
- Material assignment: read from glTF material name suffix:
  `_solid`, `_oneway`, `_death`, `_climbable`, `_ice`. Bit-OR'd.
- BVH builder: top-down median-split SAH (reuse `tools/gltf_importer/src/lib/
  bvh/v2/` already vendored — currently used only for render culling).
- `face_id` assignment: sequential index after dedup; importer guarantees
  stable order from source.
- Quantization: pick `quant_scale` per-level as
  `max(|aabb|) / INT16_MAX * 1.001` so all verts fit. Recorded in header.

## 7. Backwards-compatibility window

Until Inc 7, `Room` carries **both** `Collider[512]` (legacy) and `CollMesh*`
(new). World query funcs (`RaycastRoomSource`, ground/wall/ceiling probes)
gain a code path: if `CollMesh*` non-null, use it; else fall back to legacy.
Per-level cutover. No dual-write — a level uses one substrate or the other,
recorded in the level manifest.

## 8. Backfill / migration

- Inc 2 emits `.colmesh` for **one** test level first (a small playable room).
  Measure size, validate import. No runtime consumption yet.
- Inc 4 wires the test level through new queries with legacy still present.
- **Inc 6b** (assigned explicitly — see plan.md): bake every level in
  `data/levels/`, package each `.colmesh` into `madeline_cube_rom.dfs` via
  the existing DFS build rule, flip each level manifest's `use_collmesh`
  flag to `true`. Done when: hand-playthrough of every level reaches its
  first checkpoint.
- Inc 7 deletes legacy `Collider`/`ColliderType` once 6b reports green.

## 9. Rollback

- Per-increment: each PR keeps legacy path intact. Rollback = revert PR.
- Per-level: flip manifest flag back to `legacy`; legacy data still in repo
  until Inc 7.
- Inc 7 rollback: revert PR. Legacy structs return; manifests already on
  `colmesh`, so re-bake or revert manifests too.

## 10. Size budget (informs Inc 2 GO/NO-GO)

Targets (real-world cart pressure; revisit if largest level exceeds):
- 5 000 tris × 12 B = 60 KB triangles
- 3 000 verts × 6 B = 18 KB vertices
- ~10 000 BVH nodes × 16 B = 160 KB BVH
- Per level ≤ ~256 KB collision data. **NO-GO path** if exceeded:
  1. Inc 3 is blocked.
  2. Bake tool offers `--strip-flag=climbable` fallback (drops triangles whose
     only purpose is climb-classification and have a render twin).
  3. If still over budget: raise `quant_scale` (coarser positions); confirm
     no gameplay regression on parity test.
  4. Decision-maker for NO-GO: the engineer running Inc 2. Records outcome in
     `decisions.md` open-questions section before unblocking Inc 3.
