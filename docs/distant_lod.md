# Distant LOD (DLOD v2) — format, bake knobs, resident memory

The distant pass renders the whole-map horizon from a heavily-decimated,
compactly-packed, per-direction representation. This doc pins the binary
layout, the bake knobs, and the resident-memory math so future bakes/renderers
don't re-derive them.

## DLOD v2 binary layout (big-endian)

```
header (44 B):
  u32 magic   0x444C4F44 ("DLOD")
  u32 version 2
  u32 flags            (bit0 = per-direction)
  u32 direction_count  (1, or 4)
  u32 face_count       (total across directions)
  u32 vert_count       (total across directions; = 3 × face_count)
  u32 material_count   (≤ manifest size)
  f32 origin_x/y/z     (SHARED map-center origin, world)
  u8  reserved[4]
per-direction section (direction_count ×):
  u32 dir_face_count
  u32 dir_vert_count       (= 3 × dir_face_count)
  verts: dir_vert_count × s16 xyz     (packed (world - origin) * kLodScale;
                                      consecutive triples — face i uses
                                      verts[3i..3i+2])
  materials: dir_face_count × u8 material_id  (index into the shared manifest)
```

- **Version 2 (Inc 3 / D2):** the header `origin` is the **SHARED map-center
  origin** — ALL cells pack relative to it, so the runtime draws the whole
  distant pass under ONE camera-relative matrix (no per-cell origins, no
  per-mesh matrix rebuild). A stale v1 `.dlod` (per-cell origins) fails to
  parse at runtime (cell skipped, never misrendered). The byte layout is
  otherwise unchanged from v1.
- Vertices are packed relative to the shared origin at `kLodScale = 0.25`, so
  the int16 headroom rule is **`map_diagonal × kLodScale ≤ ~28000`** (a ~2000u
  map diagonal packs to ~500 int16 units — comfortable headroom).
- Faces are **contiguous vertex triples grouped by material at bake time**, so
  the runtime needs no sort and no indexed-draw support — it copies the triples
  into `T3DVertPacked` and builds one span-`FaceSpec` per face.
- The layout is explicit so the Python writer (`tools/writers/dlod_writer.py`)
  and the C++ parser (`src/user/gameplay/render/dlod_format.hpp`) cannot drift.

## Same-geometry, painter-sorted direction variants (Inc 1)

Each cell is decimated **once** to a single 360° mesh, then the 4 direction
sections reference the **same triangle set** reordered back-to-front along
each direction axis. The order is `(material_id, dot(centroid, dir_normal))`
ascending (stable), so material runs still form AND within-material painter
order survives (the runtime's `SortFacesByMaterial` is a stable sort). Because
all 4 directions share the same geometry, switching directions never swaps the
mesh — no popping at direction boundaries, no horizon holes. `--no-directional`
keeps the single-mesh fallback.

## Bake knobs

- `--distant-budget` (default 20): per-cell face budget (hard ceiling) for the
  single decimated mesh. Enforced by vertex-clustering on a coarsening grid,
  then by dropping the smallest coplanar groups as a last resort.
- `--no-directional`: emit a single 360° mesh instead of 4 painter-sorted
  direction variants (the fallback).
- Per-direction budget is `DEFAULT_DIRECTION_BUDGET = 20` (module constant in
  `tools/ogworld/distant_lod.py`; all 4 directions share the one decimated mesh).

## Stream radius + D5 invariant (Inc 6)

The distant tier is **dynamically streamed** by camera cell: resident = cells
within `kDistantStreamRadius` (Chebyshev, **6**) of the camera cell, evicted
outside (all direction meshes freed via the Inc 2 dedupe). The radius is
derived so the **worst-case load distance** (radius × cell − half-cell, because
distance tests hit the cell center) stays ≥ the fog-complete distance:

```
6·240 − 120 = 1320u > 1197u   (fog completes at 0.9·sqrt(kDistantMaxDist2))
```

so a cell is always fully fogged before it can become drawable — eviction/load
is invisible. **INVARIANT:** `radius ≥ ceil(fog_complete/cell + 0.5)` (asserted
by `tests/distant_streaming_contract.cpp`). On this 45-cell map the whole map
stays resident at center (radius 6 clamps to the map); the deliverable is the
architecture (boot loads a smaller initial set; larger maps scale the radius
automatically via the invariant).

## Resident-memory math

Measured on the Forsaken City bake (45 cells):

| Form | On-disk bytes | Resident estimate |
|------|---------------|-------------------|
| Baseline `*_distant.lvl` (float32) | 391,283 | ~540-650 KB (packed verts + RSPQ blocks + runs) |
| Single-mesh `.dlod` | ~17 KB | ~5× under baseline |
| 4-direction `.dlod` | ~31 KB | ~2.2-2.8× under baseline |

The dominant resident cost is the precompiled RSPQ blocks, which scale with
face count — the Inc 2 face reduction (3,807 → 787 faces) is what shrinks the
blocks; the format is the on-disk + load-time win.

## Decision rule (Inc 4/5)

After Inc 4, measure `[memory]`; if the distant share exceeds ~300 KB, fall
back to loading the 2 nearest directions only, then single-mesh. Recorded in
`docs/perf_budget.md`.

## No-block direct emit (Inc 3, streaming-memory-opt)

The distant pass no longer captures/runs RSPQ blocks. `LvlRoomRenderer` gained
`DrawRunsDirect()` (no block, no matrix-stack touch) + `SetNoBlockMode()`/
`no_block_`; distant cells set `no_block_` **before** `LoadFromDlod` (in
`dlod_loader.cpp`, both single- and multi-dir branches) so `block_` stays null,
and `DistantWorldRenderer::Render()` calls `DrawRunsDirect()` under the one
shared distant matrix (push once, emit, pop). This drops the distant pass's
~180 RSPQ blocks (~300 KB of the pool high-water mark) to **zero** — the frame
is RSP-bound, so direct emit has identical RSP cost to blocks (blocks only save
CPU time, which has headroom). The near pass still uses blocks
(`TexturedRoomRenderer::Draw`), gated by `kEnableRspqBlocks` for A/B.

## Shared per-cell vertex buffer (Inc 2 note)

The 4 direction sections reference the **same triangle set** (same-geometry,
painter-sorted variants), so all directions share one vertex buffer per cell —
switching directions never swaps the mesh and never re-uploads verts. (Inc 2's
indexed-draw re-scope was skipped as infeasible as specified; the shared-verts
property is inherent to the same-geometry bake and is preserved.)
