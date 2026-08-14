# Compressed Distant LOD (whole-map perf + RDRAM)

## Context

The two-pass open-world renderer (`.plans/n64-open-world-renderer/`) boots and
holds good frame rate when the camera faces the near ring, but **tanks when the
camera turns to the whole Forsaken City map** (observed 44 → 33 fps as
`distant_cells` goes 0 → 9-14). Two distinct problems, both rooted in the
distant representation:

1. **Draw cost (the tank).** The whole-map view draws 9-14 distant cells, each
   carrying ~85 faces (3,807 faces / 14,080 verts across 45 cells — the bake
   only collapses coplanar groups to one *arbitrary* face each, ~2.3× lighter
   than near). That is ~1,000-1,200 triangles + per-run RSP syncs + seam
   overdraw (polygons duplicated across cells) every frame, with Z off.
2. **Resident memory (the constant drag).** All 45 distant cells are loaded at
   boot and stay resident for the map lifetime: ~225 KB packed verts + ~200-300
   KB precompiled RSPQ blocks + ~100 KB runs/batches/bookkeeping ≈ **540-650 KB
   on a 4 MB RDRAM budget** (plus ~383 KB global collision, ~180-290 KB near
   ring, ~600 KB framebuffers/zbuf). The on-disk `*_distant.lvl` is float32
   LVL2 with per-face duplicated vertices, unused UVs/normals, and a per-file
   string table — the runtime immediately re-packs it into int16 anyway.

**Intended outcome:** pre-bake a heavily-decimated, compactly-packed,
per-direction distant representation that the runtime loads straight into the
existing `LvlRoomRenderer` machinery. The whole-map view holds 30 fps (≤ 33.3
ms/frame) and distant resident memory drops to roughly **1/2 of today**
(≤ ~250-300 KB) with the 4-direction representation, and up to **1/4-1/5 of
today** via the documented 2-direction or single-mesh fallbacks (the exact
number is measured in Inc 5), with no visual regression in the fogged horizon.

The **near ring stays the "close" swap** — user confirmed a **single**
compressed distant tier (the 360→1330-unit band is fogged out by 1197, so a
mid-tier has no room to justify its cost). The visible bands after this plan:

```
near ring (textured, ≤9 cells, ≤~360u)  →  distant tier (decimated, 360-1330u,
   fog completes ~1197u)  →  dropped (>1330u, fully fogged)
```

## Architectural decisions

- **D1 — Real decimation at bake time, to a per-cell face budget.** Replace
  `build_distant_lod`'s "keep `group[0]`" coplanar collapse with: (a) coplanar
  grouping keyed by `(plane, material)` (today material is ignored — a merged
  group can span materials and miscolor), (b) **union-outline extraction** per
  group (project onto the plane, trace the 2D outline, re-fan CCW; convex-hull
  fallback for non-simple outlines), and (c) **vertex-cluster decimation** on a
  coarsening grid (16 → 32 → 48 units) until the cell meets a target budget
  (default ~15-24 faces/cell avg, `--distant-budget`). Rationale: this is the
  direct fix for the whole-map tank (~4× fewer faces → ~4× fewer triangles,
  runs, and RSPQ block bytes). Alternatives rejected: quadric-error decimation
  (heavy in Python, overkill for fogged flat-color cells); texture/collision
  changes (out of scope). Keeps the existing `(verts, faces)` contract
  (`faces = (index_tuple, material_id)`, `verts = quantized world points`).
- **D2 — Compact `.dlod` container, baked already in runtime packing.**
  New binary format (big-endian, matching LVL2): **int16 positions relative to
  the cell render origin at `kLodScale`** (no float32→int16 repack at load),
  stored as **contiguous per-face vertex triples** — each triangle is 3
  consecutive `s16 xyz` verts followed by its `u8 material_id` in a side
  array, faces **pre-grouped by material at bake time** (removes the runtime
  load-time sort), no UVs/normals (distant is flat color), no per-file string
  table (materials index the shared manifest). The contiguous-triple layout is
  deliberate: the runtime's `FaceSpec`/`BatchRun`/`RunFace` machinery operates
  on **contiguous vertex spans fanned from `first_vertex`** (batch_coalesce.hpp),
  so the loader copies the triples straight into `T3DVertPacked` and builds one
  `FaceSpec(vertex_count=3, tri_count=1)` per face with **zero rework and zero
  expansion** — indexing was rejected because (a) it is incompatible with the
  span-fan machinery without new indexed-draw code, and (b) it saves almost
  nothing: the LVL2 distant bake already emits per-face fans, so the 14,080
  verts are ~3.7/face and nearly all unique. The real on-disk win is int16
  (12→6 B/vert), no UV/normal, no string table (~3×), and the real resident
  win is the Inc 2 face reduction (blocks scale with faces). A host-safe
  `dlod_format.hpp` parses the buffer zero-copy (explicit big-endian accessors);
  a thin device loader feeds the existing `LvlRoomRenderer` internals (packed
  verts, runs, RSPQ block), so the proven block-precompile +
  camera-relative-matrix draw path is unchanged. Alternatives rejected:
  extending LVL2 with a packed vertex section (keeps the float32 cruft for
  distant cells); baking RSPQ command bytes into the file (brittle across
  tiny3d/libdragon, cannot reuse `LvlRoomRenderer`, and the block must be
  captured on-target anyway).
- **D3 — Per-direction silhouettes into the existing `meshes[4]` slots.**
  Bake 4 directional meshes per cell (N/S/E/W, ~8-12 faces each — a cell's
  projection toward that direction, per Lambert `n64brew2025` §11-12). Runtime
  fills all 4 `LvlRoomRenderer*` slots and `Render()` draws the one selected by
  the already-written, host-testable `DirectionalMeshIndex` (`lod_math.hpp`
  L122-140), using the camera-facing rule near the cell center (Lambert §12) to
  avoid unstable selection. Only the facing direction draws → the whole-map
  draw cost drops ~2× further vs a single 360° mesh, and each drawn mesh is a
  tighter silhouette (less seam overdraw). All 4 directions stay resident;
  resident scales with 4 blocks/cell (honest estimate: ~2.2-2.8× under today,
  vs ~5× under today for the single-mesh compact — measured in Inc 5, with a
  documented 2-direction fallback if `[memory]` shows pressure).
- **D4 — All distant cells stay resident, but tiny. No distant streaming.**
  Target ≤ ~3-6 KB/cell resident for the 4-direction form (45 cells ≈ ≤
  250-300 KB vs ~540-650 KB today) and ≤ ~2-3 KB/cell for the single-mesh form
  (≤ ~120-150 KB). Exact numbers measured in Inc 5 via `[memory]`. Distant
  streaming (load/unload by view region) is rejected: it saves at most ~100 KB
  net after its own bookkeeping and adds pop-in/hitch risk at
  `SetCenter`/transitions for no draw-cost win.
- **D5 — Near ring, drop/fog coupling, and the two-pass architecture stay.**
  `kDistantMaxDist2`/fog coupling (`docs/perf_budget.md`) unchanged; the
  `children`/`SelectLodLevel` two-tier scaffolding stays dead (single tier per
  user decision); near pass untouched. A future bonus (raising the drop
  threshold to show more map now that far cells are cheap) is deliberately out
  of scope.
- **D6 — Rollback-safe bake/runtime transition.** The old `*_distant.lvl`
  writer + loader fallback stay until Inc 5, so a ROM booted against a stale
  bake (user hasn't re-run `make bake-forsaken-city`) still renders the
  horizon. Inc 5 removes the LVL2 distant path.

## Assumptions and answers from code

- Distant pass draws `meshes[0]` once per culled cell, back-to-front; 4-slot
  shape reserves direction variants — code @ `distant_world_renderer.cpp:99-109,
  176-196`; `distant_world_renderer.hpp:24-39`.
- `DirectionalMeshIndex` + `DirectionalIndexFromDelta` already implemented and
  host-safe — code @ `lod_math.hpp:104-140`.
- `LvlRoomRenderer` holds `verts_` (T3DVertPacked int16), `runs_`/`run_faces_`
  (coalesced, span ≤ 70), and a precompiled `rspq_block_t`; `Load()` repacks
  float32→int16 at `pos_scale` — code @ `lvl_room_renderer.{hpp,cpp}`.
- Bake contract `build_distant_lod → (verts, faces)`; current decimation keeps
  `group[0]` only; `QUANT=16`, `MERGE_COS=0.995`, `MERGE_OFFSET=8`,
  `KLOD_SCALE=0.25` — code @ `tools/ogworld/distant_lod.py:47-133`.
- Current distant on-disk ground truth (decoded LVL2 headers, verified):
  **45 cells** — 3,807 faces / 14,080 verts, avg 84.6/313, max 308; the
  `*_distant.lvl` files total **391,283 bytes (~8.7 KB/cell avg)**; near LVLs
  total **906,319 bytes (~20 KB/cell avg)** — a 2.32× byte ratio, matching the
  ~2.3× face reduction. The docs'
  "45" was right (an earlier raw listing that read 49 was a miscount; the
  package has 45 `*.lvl` + 45 `*_distant.lvl`). The "1,015 faces" doc figure is
  the **run** count (1,015 adjacent runs / 303 material-sorted), not faces.
  Inc 1 re-confirms all of this against the published artifacts.
- DFS wildcard ships only `*.lvl/.colmesh/.mappack/.manifest/.json` and the
  bake rule copies only those from staging — **`.dlod` needs a Makefile
  wildcard + `cp` line** — code @ `Makefile:60-65, 132-137`.
- `distant_lod.py` is **not** in the Forsaken City bake rule's dependency list,
  so edits to it don't re-trigger a bake — must be added — code @
  `Makefile:112-131`.
- Memory report = `mallinfo()` every 3600 frames — code @
  `rom_main.cpp:85-90`, `src/user/n64/profiler.cpp:83-105`. No 4 MB constant in
  code (documented only).
- Host tests: Pattern A = header-only single `.cpp` with its own `main()`,
  wired via `run_cpp` in `tests/run_host_tests.sh`; Python contracts via
  `run_py`; new device `.cpp` must be appended to the Makefile `src =` list
  (~line 210-250). — code @ `tests/run_host_tests.sh`, `Makefile:210-250`.
- User-confirmed: single distant tier; ~15-24 faces/cell (~4× lighter);
  per-direction variants yes; device validation by driving the camera in an
  emulator and reading `[counters]`/`[memory]` serial reports.

## Risks accepted

- **Directional popping at 45° boundaries**: the selected silhouette swaps when
  the camera crosses a direction line. Mitigated: far cells are fogged by
  ~1197 (pops are subtle), cells near their own center use the camera-facing
  rule (Lambert §12), and direction changes are stable cell-relative, not
  camera-rotation-driven. Accept; revisit if a slow 360° turn shows visible
  pops (option: hysteresis band).
- **Decimation artifacts (holes, degenerate triangles, winding flip)**: the
  union-outline + clustering path is new Python geometry code. Mitigated: a
  Python contract asserts no degenerate triangles, CCW winding preserved,
  ≥90% AABB coverage, budget enforcement; device visual smoke of the horizon.
- **New binary format drift (writer vs loader)**: mitigated by a byte-layout
  contract — the Python writer round-trip test plus a Pattern-A C++ test that
  `ParseDlod`-decodes an embedded synthetic blob byte-exact and returns -1 on
  malformed input.
- **Material merge across coplanar groups miscolors cells**: today's
  `_coplanar` ignores material; Inc 2 keys groups by `(plane, material)`.
  Contract: a two-material coplanar group stays two polygons with correct ids.
- **4 blocks/cell resident (Inc 4)**: resident roughly doubles vs the
  single-mesh compact (~2.2-2.8× under today, not 4×). Measured via `[memory]`
  in Inc 5; if distant resident exceeds ~300 KB, fall back to loading the 2
  nearest directions (dominant-axis pair) or single-mesh — the Inc 5 decision
  rule below. The whole-map **draw** cost is what the user actually observes
  tanking, and it drops ~7-9× regardless (4 dirs × 8-12 faces vs today's 85).
- **Rollback**: old `*_distant.lvl` path kept until Inc 5; Inc 5 removes it
  only after the `.dlod` bake is validated on device.
- **Re-bake required for every bake-side increment**: the user must re-run
  `make bake-forsaken-city` (or the `bake_interconnected_map.py` command) after
  Inc 2/3/4. The Makefile dependency additions (Inc 2/3) make this automatic on
  the next `make` only if the tool file timestamps change — documented in each
  increment's verification.

## Increment DAG

- Inc 1 — Baseline & bake audit (S) — depends: none — unblocks: 2, 3
- Inc 2 — Bake: real decimation to a face budget (L) — depends: 1 — unblocks: 3, 4
- Inc 3 — Compact `.dlod` format + loader (L) — depends: 2 — unblocks: 4
- Inc 4 — Per-direction silhouettes (M) — depends: 2, 3 — unblocks: 5
- Inc 5 — Memory close-out + device tuning (M) — depends: 4 — unblocks: —

```
Inc1 ──► Inc2 ──► Inc3 ──► Inc4 ──► Inc5
```

Strictly serial: Inc 3 depends on Inc 2 for two reasons. (1) **Correctness of
its memory criterion** — the dominant resident cost (precompiled RSPQ blocks)
scales with face count, so Inc 3's ~4× resident drop vs baseline is only
reachable *after* Inc 2's ~4× face reduction; on today's 3,807 faces, the
format alone delivers only ~1.2-1.5×. (2) **No merge conflicts** — Inc 2 and
Inc 3 both touch `tools/ogworld/distant_lod.py`, `tools/bake_interconnected_map.py`,
the Makefile, and `tests/run_host_tests.sh`; serializing removes the rebase
churn. Inc 4 needs Inc 2's outline machinery (per-direction silhouettes) and
Inc 3's format (4 compact direction sections). Inc 5 is the
measurement/tuning/cleanup close-out.

## Increments

### Inc 1 — Baseline measurement & bake audit (S)
**Status:** done
**Depends on:** none
**Unblocks:** 2, 3
**Done criteria:** recorded baseline in `docs/perf_budget.md` — whole-map-view
`[counters]` (`distant_cells/distant_batches/distant_vert_loads/distant_syncs`),
`[distant-cells]`, `[memory] total/used/free`; per-cell distant face/vert/run
counts + byte totals from the **current on-disk** artifacts; the 45-cell count
and the 1,015-runs-vs-3,807-faces doc discrepancy confirmed against the
published artifacts (1,015 is the **run** count, 3,807 the face count — they
are not the same quantity). No runtime code changes.

#### Files to touch

##### tools/audit_distant.py (new)
- What changes: decode the on-disk `filesystem/lvl/forsyken-city/*_distant.lvl`
  (reuse `tools/lvl_format.py`) and print a per-cell table — faces, verts,
  file bytes — plus totals. **Material-run counts are NOT reported here**: the
  run counts (1,015 adjacent / 303 material-sorted) are computed by the C++
  `CoalesceBatches`, which a Python audit cannot call; they are already known
  and stable, so the audit reports only faces/verts/bytes (the quantities the
  plan's budgets and byte math depend on).
- Function(s): `main(argv)`; reuses `LvlFile` read API.
- Data shapes: printed table + one-line totals.
- Integration points: standalone CLI, no repo imports beyond `lvl_format.py`.
- Error paths: missing dir → print error, exit 1; corrupt file → count it,
  flag it, continue.

##### tools/bake_interconnected_map.py
- What changes: extend the per-pack report dict (currently written at
  `bake_interconnected_map.py:270`) with per-cell distant stats (`faces`,
  `verts`, `bytes`) captured from `build_distant_lvl`'s stats return, so a
  fresh bake carries the numbers the audit prints.
- Function(s): the report-dict assembly in `main()`.
- Error paths: none (stats only).

##### docs/perf_budget.md
- What changes: add a "Baseline (pre compressed-LOD)" section: the recorded
  device counters, memory numbers, and the target table for Inc 5 (distant
  resident ≤ ~1/4 baseline; whole-map `distant_syncs` ≤ ~30-40/frame; frame ≤
  33.3 ms).
- Error paths: none.

#### Edge cases
- The audit must decode the artifact actually in `filesystem/` (no re-bake
  required for the baseline) so the recorded numbers match what the current ROM
  loads.
- Cell count: the published pack has **45** cells (verified — 45 `*.lvl` +
  45 `*_distant.lvl`, matching the decoded 3,807 faces / 14,080 verts). Use 45
  in all byte math; the earlier "49" listing was a miscount.
- Device numbers come from the user (drives the camera in an emulator). The
  capture sheet in `docs/perf_budget.md` must be explicit about which view to
  hold: facing the ground (baseline) vs. facing the far map corner (tank).

#### Verification
- Run: `python3 tools/audit_distant.py` → table totals match `ls -la
  filesystem/lvl/forsyken-city/*_distant.lvl` byte sums.
- Device (user): boot current ROM; hold the whole-map view; record
  `[counters]`, `[distant-cells]`, `[memory]`; record facing-the-ground too.
- Done: numbers recorded; Inc 2/4 face budgets and Inc 5 resident targets
  confirmed against the real per-cell distribution (a few cells at 150-300
  faces drive the budget ceiling).

### Inc 2 — Bake: real decimation to a per-cell face budget (L)
**Status:** done
**Depends on:** 1
**Unblocks:** 3, 4
**Done criteria:** fresh bake emits `*_distant.lvl` with per-cell faces ≤ the
`--distant-budget` ceiling (default 20; avg ~15-24, **max ≤ 20** — the budget
is a hard per-cell ceiling enforced by the smallest-group-drop fallback, not a
soft target), zero degenerate triangles, winding preserved; the decimation
Python contract passes; device whole-map `distant_batches`/`distant_syncs` drop
~4× vs Inc 1; horizon silhouettes still recognizable (user-judgment gate — the
objective proxies are the Python contract's coverage/winding/no-degenerate
asserts). No runtime code changes.

#### Files to touch

##### tools/ogworld/distant_lod.py
- What changes: replace the decimation core. `build_distant_lod(polygons,
  lod_scale, budget)` keeps its `(verts, faces)` return contract.
- Function(s):
  - `_coplanar(n1, d1, n2, d2, m1, m2)` — add material to the merge test (a
    group must share plane **and** material).
  - `_outline_of_group(group) -> List[Tuple[float,float,float]...]` (new):
    project the group's polygons onto their shared plane (2D), trace the union
    outline (edge-following; drop interior edges shared by two same-side
    polygons), emit the outline vertex ring CCW. Convex-hull fallback for
    non-simple/self-intersecting outlines (flat-color far cells tolerate it).
  - `_fan_polygon(ring) -> List[Tuple[int,int,int]]` (new): triangulate the
    ring with **ear-clipping + per-triangle CCW validation + degenerate drop**
    (a naive fan from vertex 0 inverts/overlaps concave rings and would fail
    the winding contract); convex-hull fallback for non-simple input.
  - `_decimate_to_budget(verts, faces, budget) -> (verts, faces)` (new):
    vertex-cluster on a coarsening grid (QUANT 16 → 32 → 48), drop degenerate
    (zero-area) triangles, re-fan; if still over budget, coarsen and repeat;
    never adds faces to cells already under budget. **Over-budget fallback:**
    a cell that still exceeds the budget at the coarsest grid (worst cells
    have ~300 faces on many distinct planes — clustering cannot merge across
    planes) drops its **smallest-area coplanar groups** until ≤ budget (fogged
    far cells tolerate losing small fragments); the per-cell `budget_met`
    flag is recorded in the stats.
  - `build_distant_lod(..., budget=DEFAULT_BUDGET)` — wire the new pipeline;
    `DEFAULT_BUDGET` = 20 (module constant; CLI knob in `bake_interconnected_map.py`).
    **Split candidate:** if this increment grows past one PR, split into 2a =
    outline-union (replaces `keep group[0]`) and 2b = clustering-to-budget;
    land 2a first (it alone fixes the material-miscolor + crude-collapse
    issues) and 2b second.
- Data shapes: `(verts, faces)` unchanged: `verts` = quantized world points,
  `faces` = `(idx_tuple, material_id)` (triangles after decimation).
- Integration points: `build_distant_lvl` (unchanged signature) → the same
  `emit_distant_lvl` LVL2 output.
- Error paths: outline tracing fails (non-manifold input) → convex-hull
  fallback; clustering yields 0 faces → keep the largest source face (a cell
  must stay visible); material-id preserved through every step.

##### tools/bake_interconnected_map.py
- What changes: add `--distant-budget` (default 20), pass to `build_distant_lvl`;
  per-cell distant stats already in the report (Inc 1).
- Error paths: invalid budget (≤ 0) → clamp to default.

##### tests/distant_decimation_contract.py (new, Python)
- What changes: Pattern-A-equivalent Python contract for the decimator.
- Asserts:
  - outline: two coplanar quads forming an L → 1 polygon whose AABB covers the
    union AABB;
  - material split: two coplanar faces with different `material_id` stay
    separate polygons with correct ids;
  - budget: a synthetic cell of N=80 faces decimates to ≤ budget with **zero**
    degenerate triangles;
  - winding: every emitted triangle is CCW in the group's plane frame;
  - coverage: decimated AABB covers ≥ 90% of the source AABB per axis
    (**coverage wins** over the zero-degenerate rule if they conflict — keep
    the largest non-degenerate subset that preserves coverage);
  - quantization: all verts on the QUANT grid.

##### Makefile
- What changes: add `tools/ogworld/distant_lod.py` to the Forsaken City bake
  rule's dependency list (it is currently missing, so decimation edits never
  re-trigger a bake) — the rule at `Makefile:112-131`.
- Error paths: none.

##### tests/run_host_tests.sh
- What changes: add `run_py tests/distant_decimation_contract.py` to the
  Python contracts block.

#### Edge cases
- Cells already under budget (map-edge cells with 2-15 faces): never coarsened
  further.
- Coplanar groups with holes: hull fallback (accepted for fogged far cells).
- A group spanning a cell column boundary: input is already clipped per cell by
  `chunking.py`; the decimator must not re-introduce out-of-column verts
  (cluster grid is world-space, so it can't).
- Degenerate triangles after quantization (3 collinear/duplicate verts):
  dropped; the contract pins count == 0.
- **Over-budget cells**: the budget is a hard per-cell ceiling (max ≤
  `--distant-budget`, default 20). Cells that still exceed it at the coarsest
  grid are dropped to the budget by the smallest-group fallback and flagged
  `budget_met=false` in the audit so the exceptions are visible, not silent.
  The done criterion is ≥ ~95% of cells meet the budget; the flagged remainder
  is the documented exception list.

#### Verification
- Run: `./tests/run_host_tests.sh` (all green incl. new contract); re-bake:
  `python3 tools/bake_interconnected_map.py assets/og_converted/maps/1.map
  --out-dir build/bake-fc-1200 --chunk-size 1200 --scale 0.2 --mappack-id
  forsyken-city` (or `make bake-forsaken-city`); `python3 tools/audit_distant.py`
  shows per-cell faces ≤ budget.
- ROM: no source change → rebuild is optional; device (user): re-bake, boot,
  whole-map `[counters]` vs Inc 1 baseline (~4× drop in `distant_batches`/
  `distant_syncs`), horizon silhouettes recognizable.
- Done: contract passes, bake under budget, device counters ~4× down.

### Inc 3 — Compact `.dlod` container + loader (L)
**Status:** done
**Depends on:** 2
**Unblocks:** 4
**Done criteria:** fresh bake emits `cell_XX_YY_distant.dlod` (contiguous
per-face int16 vertex triples at `kLodScale` relative to the cell origin,
pre-grouped by material, no UV/normal/string-table); `DistantWorldRenderer::Load`
loads `.dlod` with a `*_distant.lvl` fallback when absent; DFS ships `.dlod`;
host + Python contracts pass; device distant resident memory ≤ ~1/3-1/4 of the
Inc 1 baseline (`[memory]` — building on Inc 2's ~4× face reduction, which is
what shrinks the RSPQ blocks; the format itself is the on-disk + load-time
win), distant pass visually unchanged from Inc 2.

#### Files to touch

##### tools/writers/dlod_writer.py (new)
- What changes: binary writer for **DLOD v1** (big-endian, matching LVL2):
  ```
  header (44 B):
    u32 magic   0x444C4F44 ("DLOD")
    u32 version 1
    u32 flags            (bit0 = per-direction)
    u32 direction_count  (1, or 4 from Inc 4)
    u32 face_count       (total across directions)
    u32 vert_count       (total across directions; = 3 × face_count)
    u32 material_count   (≤ manifest size)
    f32 origin_x/y/z     (cell render origin, world)
    u8  reserved[4]
  per-direction section (direction_count ×):
    u32 dir_face_count
    u32 dir_vert_count       (= 3 × dir_face_count)
    verts: dir_vert_count × s16 xyz     (packed (world - origin) * kLodScale;
                                        consecutive triples — face i uses verts[3i..3i+2])
    materials: dir_face_count × u8 material_id  (index into the shared manifest)
  ```
  Vertices are packed relative to the **cell origin** (not the map origin), so
  the int16 headroom rule is `cell_extent * kLodScale ≤ 32767` — far inside the
  runtime's existing `kLodScale = 0.25` packing. Faces are **contiguous vertex
  triples grouped by material at bake time** (sorted), so the runtime needs no
  sort and no indexed-draw support — it copies the triples into `T3DVertPacked`
  and builds one span-`FaceSpec` per face (see loader). The layout is explicit
  so the Python writer and C++ parser cannot drift: every field's offset and
  width is pinned by this spec.
- Function(s): `write_dlod(directions: List[(verts, faces)], material_names,
  out_path)`; `dlod_bytes(...)` for the test.
- Data shapes: same `(verts, faces)` contract as `build_distant_lod` output.
- Integration points: called from `distant_lod.py`'s `build_distant_lvl` (or a
  new `build_distant_dlod`).
- Error paths: face/vert cap exceeded (faces > 65535, verts > 32767 in one
  direction) → raise (bake-time error, never silent truncation).

##### tools/ogworld/distant_lod.py
- What changes: `build_distant_lvl` additionally (or instead) emits the `.dlod`
  via `write_dlod`; the LVL2 `emit_distant_lvl` call stays for now (removed in
  Inc 5).
- Error paths: same as Inc 2.

##### tools/bake_interconnected_map.py
- What changes: stage `.dlod` files alongside the LVLs; include their sizes in
  the report.
- Error paths: none.

##### Makefile
- What changes: (a) `DFS_MAP_PACK_FILES` wildcard gains
  `$(wildcard filesystem/lvl/forsyken-city/*.dlod)` (`Makefile:60-65`);
  (b) the bake rule gains `cp $(FORSYKEN_CITY_OUT_DIR)/staging/*.dlod
  filesystem/lvl/forsyken-city/` and `tools/writers/dlod_writer.py` in its
  deps (`Makefile:112-137`); (c) new device source `dlod_loader.cpp` appended
  to the `src =` list (`Makefile:210-250`).
- Error paths: none.

##### src/user/gameplay/render/dlod_format.hpp (new — host-safe)
- What changes: pure, zero-copy parser; no N64 includes. Big-endian fields are
  read through explicit byte-swap accessors so the Pattern-A test is
  byte-exact on a little-endian host and the device (big-endian) reads the same
  bytes. Zero-copy = no blob copy/allocation; all views point into `data`.
- Function(s):
  ```cpp
  struct DlodVertex { int16_t x, y, z; };   // packed at kLodScale, origin-relative
  struct DlodDirection { const DlodVertex* verts; int vert_count;  // = 3 × face_count
                         const uint8_t* materials; int face_count; };
  struct DlodMesh   { float origin[3]; int direction_count; DlodDirection dirs[4]; };
  // Parse a DLOD v1 buffer into `out` (views point into `data`, zero-copy).
  // Returns direction_count parsed, or -1 on malformed input (bad magic,
  // version, truncated, vert_count != 3×face_count, material_id ≥ material_count).
  int ParseDlod(const uint8_t* data, int size, DlodMesh* out);
  ```
- Data shapes: as above.
- Integration points: called by the device loader and the Pattern-A host test.
- Error paths: every malformed field → -1 (strict; the artifact is
  bake-produced, so strictness is safe).

##### src/user/gameplay/render/lvl_room_renderer.{hpp,cpp}
- What changes: add a packed-load entry that fills the existing internals from
  already-packed int16 data (no float32→int16 repack, no material sort — the
  `.dlod` is pre-grouped):
  ```cpp
  // Load from a parsed DLOD direction. Positions are already packed at
  // `pos_scale` relative to `render_origin`; faces are contiguous vertex
  // triples pre-grouped by material. Reuses the run-coalescing + RSPQ block
  // capture path. REQUIRES pos_scale == kLodScale (the no-repack shortcut is
  // only valid at the baked scale) — assert it.
  bool LoadFromDlod(const DlodMesh& mesh, int direction,
                    const Vec3& render_origin, float pos_scale);
  ```
  Internally: assert `pos_scale == kLodScale`; copy the direction's consecutive
  triples → `T3DVertPacked` pairs (with the odd-pair padding the existing path
  already handles); build `FaceSpec[]` (face `i`: `first_vertex=3i`,
  `vertex_count=3`, `tri_count=1`, `material_id=materials[i]`); coalesce
  (`CoalesceBatches` — triples are contiguous and pre-grouped, spans never
  approach the 70-vertex cap); capture the block. **Factor the
  coalesce/counter-sums/block-capture tail of `Load()` into a shared helper**
  so the LVL and DLOD paths can't drift.
- Data shapes: unchanged internals.
- Integration points: `DistantWorldRenderer::Load` (Inc 3) / direction loop
  (Inc 4); legacy `Load(lvl_path, ...)` untouched for near cells.
- Error paths: null direction / 0 faces → `LoadFromDlod` returns false (cell
  skipped, non-fatal, matching today).

##### src/user/gameplay/render/dlod_loader.cpp (new — device)
- What changes: thin DFS/rom glue.
- Function(s): `bool LoadDistantCellDlod(const char* pack_dir, const char*
  chunk, const Vec3& render_origin, float pos_scale, LvlRoomRenderer* out)` —
  builds `rom:/lvl/<pack_dir>/<chunk>_distant.dlod` via the same
  `LocalizePath`/`dfs_load` pattern as `LvlRoomRenderer::Load` (on host,
  `build_dir/<chunk>_distant.dlod`); reads + `ParseDlod`, falls back to
  `LvlRoomRenderer::Load` on `<chunk>_distant.lvl` when the `.dlod` is absent;
  calls `LoadFromDlod` for direction 0.
- Integration points: called by `DistantWorldRenderer::Load`.
- Error paths: file open fail → fallback; parse -1 → fallback to `.lvl`.

##### src/user/gameplay/render/distant_world_renderer.cpp
- What changes: `Load()` prefers `rom:/lvl/<pack>/<chunk>_distant.dlod`
  (host path via `build_dir` when provided), falls back to `<chunk>_distant.lvl`
  when the `.dlod` is absent (rollback-safe). Fills `meshes[0]` for now (Inc 4
  fills all four). `EntryCount()==0` load-once gate unchanged.
- Error paths: missing both → entry skipped (non-fatal, today's behavior).

##### Tests
- `tests/dlod_format_contract.cpp` (new, Pattern A): embed a synthetic DLOD v1
  blob; `ParseDlod` returns exact counts + byte-exact values (explicit
  big-endian reads); malformed blobs (bad magic, truncated, `vert_count !=
  3×face_count`, `material_id ≥ material_count`, bad counts) → -1.
- `tests/dlod_format_contract.py` (new, Python): writer → parse-back →
  byte-layout assertions; for a real bake, the triangle set decoded from a
  `.dlod` equals the float32 `*_distant.lvl` triangle set (geometry
  equivalence, same as the existing coalesce contracts' replay pattern).
- `tests/distant_lod_contract.py` (existing): extend to also assert `.dlod`
  verts are int16-packed at `kLodScale` relative to the cell origin and within
  int16 range.
- `tests/run_host_tests.sh`: wire the two new tests.

#### Edge cases
- Endianness: big-endian everywhere; the parser uses explicit byte-swap
  accessors and asserts the magic, so a little-endian mistake fails loudly in
  the host test.
- A cell with 0 geometry in a direction: `dir_face_count = 0` → `LoadFromDlod`
  returns false → slot skipped (non-fatal).
- Triangle fan parity: `T3DVertPacked` loads in vertex pairs; the existing
  padding logic is reused, and each face is 3 verts (pair-rounded), so no new
  parity cases.
- `pos_scale != kLodScale`: asserted in `LoadFromDlod` (a wrong scale would
  silently misplace geometry).
- Stale bake (no `.dlod` yet): fallback keeps the ROM rendering — the DFS
  wildcard also still ships `*_distant.lvl` until Inc 5.

#### Verification
- Run: `./tests/run_host_tests.sh` (all green incl. the two new tests);
  re-bake; `ls filesystem/lvl/forsyken-city/*.dlod` present and sized; ROM
  builds clean (new sources in Makefile `src=`).
- Device (user): boot (with a fresh bake), whole-map view — distant pass
  visually unchanged from Inc 2, `[memory] used=` ≤ ~1/3-1/4 of the Inc 1
  baseline (Inc 2's face reduction is what shrinks the blocks; this increment
  adds the format + on-disk savings).
- Done: format + loader shipped, memory target hit, no visual regression.

### Inc 4 — Per-direction silhouettes (M)
**Status:** done
**Depends on:** 2, 3
**Unblocks:** 5
**Done criteria:** `.dlod` files carry 4 directions (each ≤ ~8-12 faces);
`DistantWorldRenderer::Load` fills all 4 `meshes[d]` and the destructor/
reload cleanup frees all distinct direction meshes (no leak); `Render()`
selects via `DirectionalMeshIndex` (camera-facing rule near cell center) and
draws one direction per cell; whole-map `distant_batches`/`distant_syncs`
(the selected direction's material-run draw counters; the per-cell Draw call
count stays 1) drop ~2× vs Inc 3; a slow 360° turn shows no visible popping.

#### Files to touch

##### tools/ogworld/distant_lod.py
- What changes: per-direction silhouette extraction.
- Function(s): `_direction_silhouette(polygons, dir_index, budget) ->
  (verts, faces)` (new): keep faces whose outward normal faces `dir_index`
  (within ±45°), plus the group outlines touching that side; decimate to the
  per-direction budget (~8-12) with the Inc 2 machinery. `build_distant_dlod`
  emits 4 directions via `write_dlod(..., direction_count=4)` (bit0 set).
- Data shapes: per-direction `(verts, faces)`.
- Integration points: called in place of the single-mesh `build_distant_lod`
  when directional mode is on (default on from Inc 4; `--no-directional` knob
  to fall back).
- Error paths: a direction with no facing geometry → empty list (dir skipped).

##### tools/writers/dlod_writer.py
- What changes: `direction_count=4` mode (the Inc 3 layout already reserves the
  per-direction sections and the flags bit).
- Error paths: unchanged.

##### src/user/gameplay/render/distant_world_renderer.cpp
- What changes:
  - `Load()`: per cell, `LoadFromDlod` for each of the 4 directions into
    `meshes[0..3]`; empty direction → `meshes[d] = nullptr`.
  - **Cleanup (MUST-FIX):** update `~DistantWorldRenderer()` and the reload
    path in `Load()` to free **all distinct** `meshes[d]` (today only
    `meshes[0]` is freed, and the destructor's comment explicitly flags that
    distinct directional variants must revisit this — with 4 real meshes, 3 of
    every 4 would leak on a 4 MB target). Add a `FreeEntries()` helper that
    loops `0..kMaxDirMeshes` and nulls each slot.
  - `Render()`: for each culled cell,
    `int d = DirectionalMeshIndex(cam_pos, cell.origin, cam_dir,
    kDirectionCloseThreshold)`; draw `meshes[d]` if non-null, else `meshes[0]`
    if non-null, else skip. `kDirectionCloseThreshold` ≈ 0.5 × cell size (120
    world units).
- Function(s): `Load`, `Render` (draw loop only — culling/sort unchanged),
  `FreeEntries` (new).
- Data shapes: `DistantLodEntry.meshes[4]` becomes real (distinct pointers).
- Integration points: `DirectionalMeshIndex` from `lod_math.hpp` (exists).
- Error paths: all four directions null → skip (matches today's no-mesh skip);
  `DirectionalMeshIndex` returns 0..3 always (clamped by its own logic).

##### src/user/gameplay/render/dlod_loader.cpp
- What changes: `LoadDistantCellDlod` gains a direction index parameter (or the
  caller loops) so each `meshes[d]` is loaded from its direction section.
- Error paths: unchanged.

##### Tests
- `tests/directional_lod_contract.cpp` (new, Pattern A): `DirectionalMeshIndex`
  picks the correct slot for the camera at N/S/E/W; uses the camera-facing rule
  when within `close_threshold` of the cell center (Lambert §12).
- `tests/dlod_format_contract.py`: assert 4 directions decode; each direction's
  face count ≤ per-direction budget.
- `tests/distant_decimation_contract.py`: per-direction coverage — each
  direction's mesh AABB covers ≥ 90% of the cell's projection from that
  direction.

#### Edge cases
- Direction boundary popping: cells are 240 units, far cells fogged by ~1197 —
  accepted (see Risks). The near-center camera-facing rule prevents jitter when
  the camera is on top of a cell.
- Cells at the map edge: some directions have no geometry → null slots; the
  fallback order (`meshes[d]` → `meshes[0]` → skip) keeps them visible from
  valid directions.
- **Resident decision rule (4 vs 2 directions):** after Inc 4, measure
  `[memory]`; if the distant share exceeds ~300 KB, fall back to loading the 2
  nearest directions only (`meshes[axis]` + `meshes[axis+1]` from
  `DirectionalIndexFromDelta`) and re-measure; if still over, single-mesh.
  The decision is recorded in `docs/perf_budget.md`. (Estimate: 4 dirs ≈
  2.2-2.8× under baseline; 2 dirs ≈ 3-4×; single-mesh ≈ 5×.)

#### Verification
- Run: `./tests/run_host_tests.sh` (all green incl. new contract); re-bake;
  ROM builds clean.
- Device (user): whole-map view `distant_batches`/`distant_syncs` ~2× below
  Inc 3; slow 360° turn — no popping; `[memory]` recorded for the Inc 5
  decision rule. **Seam cells** (polygons duplicated across cell columns) are
  the overdraw root cause named in Context — explicitly walk a seam-heavy
  region and confirm the per-direction silhouettes don't leave holes or
  doubled geometry at cell boundaries.
- Done: directional draw live, draw cost ~2× down, no visible popping, no
  direction-mesh leak (repeated `SetCenter`/reload cycles hold `[memory]` flat).

### Inc 5 — Memory close-out + device tuning (M)
**Status:** done
**Depends on:** 4
**Unblocks:** —
**Done criteria:** device `[memory]` shows the distant share at the Inc 4
decision rule's target — **≤ ~1/2 of the Inc 1 baseline with 4 directions
(≤ ~300 KB), and ≤ ~1/4-1/5 with the 2-direction or single-mesh fallback**;
whole-map view holds ≤ 33.3 ms (30 fps) with distant ≤ 12 ms; `[counters]`
targets met; the LVL2 distant path is removed; `batches_` freed on the run
path; docs finalized.

#### Files to touch

##### src/user/gameplay/render/lvl_room_renderer.cpp
- What changes: free `batches_` once the coalesced-run path is active
  (Explore-1 finding: the per-face batch array stays allocated forever even
  though the run + block path never reads it — ~60 KB across 45 distant cells).
  `FreeBatches()` after a successful coalesce; keep it only when coalescing
  failed (fallback path needs it). `Free()` stays idempotent.
- Error paths: `Draw()`'s run-path gate (`IsActiveRunPath`) already guards the
  null `batches_`; the fallback path still has it.

##### src/user/gameplay/render/distant_world_renderer.cpp
- What changes: drop the `*_distant.lvl` fallback (`.dlod` is now the only
  distant artifact); keep the non-fatal skip for missing files. If device
  numbers demand: tune `kCullMargin`, or tighten `kDistantMaxDist2` (fog
  follows automatically via the existing coupling), or apply the Inc 4
  2-direction/single-mesh fallback.
- Error paths: missing `.dlod` → skip (never crash).

##### tools/ogworld/distant_lod.py
- What changes: remove `emit_distant_lvl` and the `*_distant.lvl` output — the
  bake emits `.dlod` only.
- Error paths: none (bake-side).

##### tests/distant_lod_contract.py
- What changes: **migrate this test to the `.dlod` artifact.** It currently
  bakes and globs `staging/*_distant.lvl` (`distant_lod_contract.py:128`),
  decoding float32 verts via `read_distant_verts`. After Inc 5 removes the
  `*_distant.lvl` output, this test would fail to find any distant files and
  silently stop enforcing the int16-at-`kLodScale` invariant. Rewrite
  `read_distant_verts` to parse `*_distant.dlod` (reuse the DLOD layout from
  `docs/distant_lod.md` / `dlod_format.hpp`) and keep the same int16-range
  assertions.
- Error paths: no `.dlod` files → fail loudly (the invariant must stay
  enforced).

##### docs/perf_budget.md
- What changes: final per-phase budget + tuning knobs: `kDistantBudget` /
  `kPerDirectionBudget` (bake), `kDirectionCloseThreshold`, `kCullMargin`;
  the baseline-vs-final measurement table; note the Ares timing caveat (Ares
  numbers are not real timing — the user validates on their emulator with the
  serial reports).
- Error paths: none.

##### docs/distant_lod.md (new)
- What changes: document the DLOD v1 binary layout (so future bakes/renderers
  don't re-derive it), the bake knobs, and the resident-memory math.
- Error paths: none.

#### Edge cases
- Removing the LVL2 distant fallback means a ROM built against a stale
  (`.dlod`-less) bake shows no distant pass — documented; the Makefile re-bake
  dependency (Inc 2/3) makes the fresh bake the normal path.
- `batches_` free must not break the per-face fallback path (only freed when
  runs are active).

#### Verification
- Run: `./tests/run_host_tests.sh` (full suite green) + `./compile-rom.sh`
  clean (from-scratch after the header/struct changes — `build-gotchas`).
- Device (user): whole-map view — `[counters]` at target
  (`distant_cells` ≤ ~15, `distant_syncs` ≤ ~30-40), `[memory]` distant share
  at the Inc 4 decision rule's target (≤ ~1/2 baseline with 4 directions,
  ≤ ~1/4-1/5 with a fallback), 30 fps held; 360° turn + full-map walk, no
  popping/holes.
- Done: all targets met, LVL2 distant path gone, docs finalized.

## Cross-cutting verification

- **Every increment**: `./tests/run_host_tests.sh` green and
  `./compile-rom.sh` clean. After any header struct change, do a from-scratch
  clean build (`.agents/common-mistakes`/repo memory: stale-object ODR → crash).
- **Re-bake after every bake-side increment** (Inc 2/3/4/5). The Makefile dep
  additions make `make` re-bake on tool changes; otherwise run the documented
  `bake_interconnected_map.py` command. A stale bake is the #1 source of
  "works in plan, broken in ROM".
- **Device measurement sheet** (user, per `docs/perf_budget.md`): record
  `[counters]`, `[distant-cells]`, `[memory]` at each increment in the
  whole-map view and facing the ground. Track: `distant_batches`/`distant_syncs`
  (draw cost), `[memory] used=` (residency).
- **Ares timing is a proxy, not proof**: Ares runs the ROM at ~0.1 fps under
  software paraLLEl-RDP; use Ares for visual smoke + counters, and rely on the
  user's emulator timing for frame-rate conclusions (per `docs/perf_budget.md`).

## Standards / common-mistakes referenced

- `.agents/common-mistakes/og-map-polygon-winding.md` — applies to: Inc 2/3/4
  — decimation and the DLOD writer must preserve CCW winding (contract asserts
  it).
- `.agents/common-mistakes/dfs-path-prefix.md` — applies to: Inc 3/4 — the
  `rom:/lvl/forsyken-city/<chunk>_distant.dlod` path must follow the existing
  `rom:/` convention; the host `build_dir` path must localize identically.
- `.agents/common-mistakes/missing-player-start-init.md` — applies to: Inc 3/4
  — `DistantWorldRenderer::Load` keeps the `EntryCount()==0` boot gate; the
  distant load must not change boot/respawn ordering.
- `.agents/common-mistakes/camera-respawn-reset.md` — applies to: Inc 4 —
  direction selection is render-only and must not touch respawn camera reset.

## Open questions (CONSIDER from review)

- **Fog number**: the plan says fog completes at ~1197 (`sqrt(kDistantMaxDist2)
  × 0.9`); the `lod_math.hpp` comment still reads "fog onset ≈ 1329" (stale,
  pre-fixup). The runtime fog range is `sqrt(kDistantMaxDist2) × 0.4 → 0.9` =
  ~532 → 1197, and the invariant (fog completes before the ~1330 drop) holds
  either way. Inc 5 should correct the `lod_math.hpp` comment.
- **Decimation contract conflict**: "zero degenerate" vs "≥90% coverage" can
  conflict under heavy quantization — the plan pins coverage as binding (Inc 2
  contract), but Inc 2's device tuning should confirm the choice doesn't show
  holes.
- **`LoadFromDlod` vs `Load()` shared tail**: the plan factors the
  coalesce/block-capture tail into a shared helper (Inc 3) — worth doing
  early so the two paths can't drift.
- **Inc 3 counter verification**: `distant_batches`/`distant_syncs` measure
  material-run counts, which stay ~constant per cell under the format change;
  the observable Inc 3 wins are `[memory]` (resident) and load-time work, not
  the draw counters (those drop in Inc 2 and Inc 4). Keep the Inc 3 done
  criteria on the memory/format axis.
- **Seam-overdraw root cause**: Context names "seam overdraw (polygons
  duplicated across cells)" as a draw-cost contributor, but no increment
  removes the root cause (a polygon is assigned to every cell column its AABB
  intersects). Per-direction silhouettes partially self-solve it (a cell only
  keeps faces facing its direction). Inc 4's device walk explicitly checks a
  seam-heavy region (see Inc 4 verification) so the residual overdraw is
  observed, not assumed gone.
- **Inc 2 split**: outline-union + ear-clipping + vertex-clustering +
  budget-fallback is a lot of new Python geometry. The function specs are
  detailed enough for one PR, but if it grows, split into 2a = outline-union
  and 2b = clustering-to-budget (noted in the Inc 2 function spec).

## Out of scope

- Compact/packed format for **near** cells (float32 LVL2, int16 packing is a
  future memory win; the near ring is bounded at ≤ 9 cells and ~180-290 KB
  resident).
- Distant-cell **streaming** (load/unload by view region) — rejected (D4).
- Raising `kDistantMaxDist2` to reveal more of the map now that far cells are
  cheap — a separate visual decision, deliberately deferred.
- Two-tier distant (mid/far) — rejected by user.
- Textured distant cells (flat per-material color stays).
- Gameplay, player model, collision mesh, near-pass rendering.
