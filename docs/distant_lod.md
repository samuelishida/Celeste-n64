# Distant LOD (DLOD v1) — format, bake knobs, resident memory

The distant pass renders the whole-map horizon from a heavily-decimated,
compactly-packed, per-direction representation. This doc pins the binary
layout, the bake knobs, and the resident-memory math so future bakes/renderers
don't re-derive them.

## DLOD v1 binary layout (big-endian)

```
header (44 B):
  u32 magic   0x444C4F44 ("DLOD")
  u32 version 1
  u32 flags            (bit0 = per-direction)
  u32 direction_count  (1, or 4)
  u32 face_count       (total across directions)
  u32 vert_count       (total across directions; = 3 × face_count)
  u32 material_count   (≤ manifest size)
  f32 origin_x/y/z     (cell render origin, world)
  u8  reserved[4]
per-direction section (direction_count ×):
  u32 dir_face_count
  u32 dir_vert_count       (= 3 × dir_face_count)
  verts: dir_vert_count × s16 xyz     (packed (world - origin) * kLodScale;
                                      consecutive triples — face i uses
                                      verts[3i..3i+2])
  materials: dir_face_count × u8 material_id  (index into the shared manifest)
```

- Vertices are packed relative to the **cell origin** at `kLodScale = 0.25`, so
  the int16 headroom rule is `cell_extent * kLodScale ≤ 32767`.
- Faces are **contiguous vertex triples grouped by material at bake time**, so
  the runtime needs no sort and no indexed-draw support — it copies the triples
  into `T3DVertPacked` and builds one span-`FaceSpec` per face.
- The layout is explicit so the Python writer (`tools/writers/dlod_writer.py`)
  and the C++ parser (`src/user/gameplay/render/dlod_format.hpp`) cannot drift.

## Bake knobs

- `--distant-budget` (default 20): per-cell face budget (hard ceiling) for the
  single-mesh form. Enforced by vertex-clustering on a coarsening grid, then by
  dropping the smallest coplanar groups as a last resort.
- `--no-directional`: emit a single 360° mesh instead of 4 per-direction
  silhouettes (the Inc 4 fallback).
- Per-direction budget is `DEFAULT_DIRECTION_BUDGET = 12` (module constant in
  `tools/ogworld/distant_lod.py`).

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
