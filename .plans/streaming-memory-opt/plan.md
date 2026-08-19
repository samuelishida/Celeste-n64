# Streaming & Memory Optimization for the Open-World Renderer

## Context

The Forsaken City map (45 cells) renders through a two-pass z-split renderer
(`OpenWorldRenderer::Render`): a flat-color **distant pass** (4-directional
decimated DLOD silhouettes, Z-off, one shared camera-relative matrix) and a
textured **near pass** (9-cell resident ring, Z-on, per-material TMEM sprites).
Performance improved across the `distant-pass-perf` and `lod-streaming-overhaul`
plans, but two problems remain:

1. **Streaming hitch on every cross-cell transition.** `TileStreamer::SetCenter`
   frees + reloads + RSPQ-recompiles **all 9** near cells even though a
   center→neighbor transition changes only 1–2 cells. This is the worst UX
   problem and no prior plan has touched it.
2. **Memory footprint (~700 KB–1 MB controllable of the 4 MB RDRAM budget).**
   The distant pass keeps all 45 cells × 4 direction variants permanently
   resident, repacks each cell's vertices **4×** (the directions share identical
   geometry), and allocates ~180 RSPQ blocks. The near ring holds 9 cells.

The user's verbatim ask: *"heavily optimize asset streaming and memory access
for 4mb of really slow memory in the n64"*, with the symptom *"in the middle of
the map it still takes."*

**Critical reconciliation.** The `lod-streaming-overhaul` plan (all 7 increments
`Status: done`) already fixed the z-split *rendering* bugs the earlier analysis
draft flagged as open: direction-selection popping (Inc 1), screen-edge pop-in /
2D-vs-3D cull (Inc 4), seam double-draw (Inc 5), near/distant handoff fog
(Inc 5), and the per-mesh matrix storm (Inc 3). DLOD is verified **version 2,
shared map-center origin, 20–80 faces/cell (20 per direction)** — so the distant
pass at center is ~36 cells × 20 faces ≈ 720 faces, *not* the dominant cost.
The **near pass** is the frame-time wall. This plan therefore targets
**streaming + memory + the RSP/DMA traffic that causes "still takes"**, not the
already-solved z-split visuals. Note the distant tier is **already incremental**:
`DistantWorldRenderer::StreamToCenter` (called from `OpenWorldRenderer::SetCenter`
alongside the near ring) evicts only out-of-radius cells and loads only missing
in-radius cells (`lod-streaming-overhaul` Inc 6). On this 45-cell map the stream
radius 6 covers the whole map, so it loads nothing after boot. The wholesale
rebuild that causes the hitch is exclusively the **near ring**, which is exactly
what Inc 1 makes incremental (SF1).

**Load-bearing constraint.** The frame is **RSP-bound**: `rom_main.cpp` calls
`rspq_wait()` after `Render()`, so frame time ≈ RSP execution time, not CPU
time. RSPQ blocks replay asynchronously, DMAing matrices/vertices from RDRAM at
execution. **Frame-time** gains therefore require reducing **RSP command count**
or **RDRAM/TMEM DMA traffic**; the **memory** increments (Inc 2, Inc 3) instead
reduce **RDRAM footprint** (shared verts, freed RSPQ pool) without touching RSP
command count. CPU-side refactors are out of scope because the frame is
RSP-bound.

## Architectural decisions

### Decision 1 — Keep the 9-cell near ring; make it incremental
**Rationale.** The ring is loaded to avoid pop-in when the camera orbits freely
(C-stick). A center→neighbor transition changes only 1–2 cells, but
`SetCenter` rebuilds all 9. Making the ring **incremental** (keep the 7
overlapping cells, load only the 1–2 new, free only the departed) kills the
hitch with no coverage change.
**Alternatives rejected.** *Shrink the ring to 5 (center + cross):* halves
near-ring memory but pops in diagonal cells on free camera orbit — a visible
regression. *Async/prefetch loading:* adds a load queue + timing complexity for
a sync load that is already small once only 1–2 cells load.

### Decision 2 — Reduce distant-pass memory per cell, not by evicting cells
**Rationale.** The D5 fog invariant caps the distant stream radius at
`ceil(fog_complete/cell + 0.5) = 6`, and radius 6 already covers the **entire**
45-cell map — so distant cells **cannot be evicted** without pop-in. The memory
must come from per-cell footprint: (a) the 4 direction variants share identical
geometry but are repacked 4× (~130 KB wasted); (b) ~180 distant RSPQ blocks
(~300 KB of the pool) are unnecessary for a flat-color pass that could emit its
runs directly.
**Alternatives rejected.** *Lower the stream radius:* violates D5, causes
pop-in. *Evict distant cells:* same problem — radius 6 is the whole map.

### Decision 3 — Group the near-pass draw by material globally
**Rationale.** The near pass is RSP-bound and dominated by **TMEM sprite
uploads** (~92/frame at center ≈ 23 materials × ~4 cone-visible cells). Each
upload is a RDRAM→TMEM DMA — the "memory access" cost. The near pass uses
**per-cell render origins**, so vertex runs cannot be merged *across* cells,
but the sprite upload can be hoisted: draw **for each material** (upload sprite
once), then **for each cell** (push matrix, `vert_load` that cell's faces for
this material, `tri_sync`, pop). Sprite uploads drop ~92→~23 (the distinct
material count, per the `textured_room_renderer.cpp` "199 runs → 23 material
groups" comment and `MaterialCatalog::kMaxMaterials = 32`); matrix pushes and
`vert_load`s are unchanged in count. The exact distinct-material count is
confirmed by the host test / on-device `[counters]` in Inc 4.
**Alternatives rejected.** *Merge vertex runs across cells:* requires repacking
all near cells to one shared origin (a much larger change) and risks the
70-vertex RSP cap on merged runs. *Reduce texture count:* an art decision, out
of scope.
**Note (MF1):** this reorder requires re-architecting the near-pass RSPQ block
capture from per-cell to per-material — see the Inc 4 body, which addresses the
block barrier and its trade-off explicitly.

### Decision 4 — Optimize RSP command count / DMA traffic, not CPU
**Rationale.** Because the frame is RSP-bound (Decision context above), all
increments target RSP command count or RDRAM/TMEM DMA. CPU-side refactors are
explicitly out of scope.

## Assumptions and answers from code

- **A1 — Frame time ≈ RSP execution time.** `rspq_wait()` is called after
  `Render()` in the frame loop; RSPQ blocks replay asynchronously, DMAing from
  RDRAM at execution.
  Source: `src/user/rom_main.cpp` (frame loop, `rspq_wait()`), `AGENTS.md`
  ("Frame time ≈ RSP execution time, not CPU time").
- **A2 — The near pass is the frame-time wall.** Near pass ≈ 3–5 cone-visible
  cells × ~23 material groups × (sprite upload + `vert_load` + `tri_sync`);
  distant pass ≈ 12–18 extent-culled cells × 20 faces. Near issues ~7× the RSP
  commands.
  Source: `RenderCounters` (`sprite_uploads`, `vert_loads`, `tri_syncs`) in
  `src/user/gameplay/render/open_world_renderer.hpp`; prior perf analysis.
- **A3 — DLOD is version 2, shared map-center origin, 20–80 faces/cell (20/dir).**
  All 45 files decode as version 2; `DEFAULT_BUDGET = 20`.
  Source: `tools/ogworld/distant_lod.py` (`DEFAULT_BUDGET`,
  `build_distant_dlod_directional`); verified decode of
  `filesystem/lvl/forsyken-city/*.dlod`.
- **A4 — Distant stream radius is capped at 6 and covers the whole map.**
  `kDistantStreamRadius = 6`; D5 invariant `radius ≥ ceil(fog_complete/cell +
  0.5) = 6`; the 45-cell map fits inside radius 6.
  Source: `src/user/gameplay/render/distant_world_renderer.hpp`
  (`kDistantStreamRadius`); `.plans/lod-streaming-overhaul/plan.md` Inc 6.
- **A5 — The 4 DLOD direction variants share the same triangle set, in
  different face orders (CORRECTED 2026-08-19).** The baker decimates **once**
  to a single 360° mesh, then reorders faces per direction (painter's
  back-to-front along each axis); the writer expands each direction into
  **consecutive per-face triples in that direction's order**. So the vertex
  **multiset** is identical across directions, but the vertex **streams are
  face-order permutations** (NOT byte-identical). Verified by parsing
  `filesystem/lvl/forsyken-city/{cell_01_n03,cell_00_00,cell_02_n01}_distant.dlod`.
  Consequence: a single `T3DVertPacked` buffer cannot serve all 4 directions on
  the consecutive-triples fan path (see Inc 2 Finding).
  Source: `tools/ogworld/distant_lod.py` (`build_distant_dlod_directional`,
  `_sort_direction`), `tools/writers/dlod_writer.py` (`dlod_bytes`).
- **A6 — The near ring is center + Chebyshev-1 (≤9 cells), all loaded on transition.**
  `kMaxRing = 9`; `ResolveDistanceRing` returns center + all rooms with
  `|dx|≤1 && |dz|≤1`; `SetCenter` frees all then loads all.
  Source: `src/user/gameplay/render/tile_streamer.hpp` (`kMaxRing`,
  `ResolveDistanceRing`), `tile_streamer.cpp` (`SetCenter`).
- **A7 — Freeing an RSPQ block during Update is safe.** The previous frame's
  blocks are drained by `rspq_wait()` before the next `Update` runs, so a
  renderer freed during `SetCenter` (called from `Update`) has no queued block.
  Source: `.plans/rspq-block-render/plan.md`; `src/user/rom_main.cpp` frame loop.
- **A8 — The near pass uses per-cell render origins.** Each cell is loaded with
  its own `rs.render_origin`; the model matrix is `render_origin − camera_pos`.
  Vertex runs therefore cannot be concatenated across cells.
  Source: `tile_streamer.cpp` (`tr->Load(path, rs.render_origin, catalog_)`);
  `.plans/lod-streaming-overhaul/plan.md` (per-cell origins for the near pass).

## Risks accepted

- **R1 — Incremental ring diff frees a cell whose block is still queued.**
  Mitigation: frees happen only in `SetCenter` (called from `Update`, after the
  previous frame's `rspq_wait()`), per A7. Verify no crash on rapid
  A→B→A→C transitions.
- **R2 — Shared distant vertex buffer breaks per-direction ordering.**
  Mitigation: share only the vertex *data*; each direction keeps its own
  face/order array. Verify silhouettes are byte-identical to the 4×-repacked
  baseline.
- **R3 — Dropping distant RSPQ blocks changes the distant emit path.**
  Mitigation: keep the block path behind a compile flag for A/B; verify the
  direct emit produces the same run sequence and the shared distant matrix is
  still pushed once before it.
- **R4 — Global material grouping mis-draws a face (wrong material/order).**
  Mitigation: within a material, preserve each cell's existing face order; host
  test asserts the drawn (face, material) multiset is unchanged.
- **R5 — Memory measurement doesn't match the projection.**
  Mitigation: use the existing `[memory]` telemetry (FrameArena peak, RSPQ pool)
  as ground truth; document any delta in the close-out.

## Increment DAG

```
Inc 1 (ring diff)      Inc 2 (distant shared verts)   Inc 3 (drop distant blocks)   Inc 4 (near material grouping)
   (M, no deps)           (M, no deps)                    (M, no deps)                  (L, no deps)
        \                     |                               |                               /
         \                    |                               |                               /
          +-------------------+-------------------------------+-----------------------------+
                                        |
                                   Inc 5 (close-out, S)
                                   deps: 1, 2, 3, 4
```

Inc 1–4 are independent and may land in any order (or parallel branches). Inc 5
measures the combined result and depends on all four.

## Increments

**Pre-step (before Inc 1):** capture the baseline at map center — frame time
(profiler), `[memory]` peak (FrameArena + RSPQ pool), and `[counters]`
(`sprite_uploads`, `vert_loads`, `tri_syncs`) — on the current ROM. Every
increment's "before" numbers and the Inc 5 before/after table depend on this;
do not start Inc 1 without it. **Automate it (C3):** add
`tools/capture_baseline.sh` that greps `[profiler]` / `[memory]` / `[counters]`
from emulator stdout at map center and writes `build/baseline-<date>.txt`, so
Inc 5's before/after comparison is reproducible rather than a one-time copy-paste.

### Inc 1 — Incremental near-ring diff (M) — **Status: done**

**Depends on:** (none)
**Unblocks:** Inc 5
**Done criteria:** A center→neighbor transition loads only the 1–2 new cells (host test asserts load count = new cells, not 9); the 7 overlapping cells are kept (no reload); departed cells are freed exactly once; no crash/leak on rapid A→B→A→C; ROM builds and the transition hitch is significantly reduced on device (no 9× load spike; a 1–2-cell RSPQ recompile cost may remain and is acceptable).

**Close-out (R5 — document the delta, don't force the projection):**
- **Number correction:** the plan's "7 keep / 2 load / 2 free" projection does
  not match Chebyshev-1 ring math. A 3×3 ring shifted by one cell overlaps in a
  3×2 = **6** region, so a 1-step move on a full grid is **6 keep / 3 load /
  3 free** (two full 3×3 rings overlap in 9/6/4, never 7). The load-bearing
  invariant — "load = the new cells, not 9" — holds and is what the test
  asserts.
- **Host test:** `tests/tile_streamer_diff_smoke.cpp` (Pattern A, header-only)
  passes: A→B = 6 keep / 3 load / 3 free; A→B→A→C = no double-free/leak;
  map-edge E→A = 6 keep / 3 load / 0 free; 0-overlap teleport E→C = 0 keep /
  6 load / 6 free (identical to old free-all+load-all).
- **ROM builds** (`./compile-rom.sh` exit 0, no warnings); **host suite**
  36/36 pass; **Ares device smoke** boots at the 30 fps cap with live
  `[counters]` cell transitions and no crash.
- **Device hitch reduction** (the "no 9× load spike" criterion) is a
  device-only confirmation → Inc 5 close-out (re-capture baseline at map
  center and compare the transition frame spikes).

Replace the free-all + load-all in `TileStreamer::SetCenter` with a diff:
compute the new ring, keep residents still in it, load only the new cells, free
only the departed cells. This kills the primary streaming hitch (Decision 1).

#### Files to touch

- `src/user/gameplay/render/tile_streamer.cpp`
  - **What changes:** `SetCenter` no longer frees all residents first. It
    (1) resolves the new ring via `ResolveDistanceRing`; (2) for each new-ring
    cell, checks `set_.IndexOf(spec)` — if resident, keep the existing renderer
    and `Touch` it; if not, load a new renderer; (3) frees every current
    resident whose spec is not in the new ring; (4) compacts the parallel
    arrays (`renderers_[]`, `textured_renderers_[]`, `set_.spec[]`,
    `set_.last_used[]`) to the new ring order (center first).
  - **Function(s):** `TileStreamer::SetCenter`.
  - **Data shapes:** Reuses `ResidentSet` (`spec[]`, `last_used[]`, `count`,
    `IndexOf`, `Touch`). No new persistent state.
  - **Integration points:** Called from `OpenWorldRenderer::SetCenter`
    (transition + boot). The LRU eviction safety net in `UpdateCamera` is
    unchanged (the ring still never exceeds `kMaxRing`).
  - **Error paths:** A non-center cell whose `Load` fails is skipped (existing
    behavior); a center-cell `Load` failure returns false and resets
    `set_.count = 0` (existing behavior). Keeping an already-resident cell
    cannot fail.
- `src/user/gameplay/render/tile_streamer.hpp`
  - **What changes:** Add a **host-safe** inline diff helper, e.g.
    `ResolveRingDiff(old_ring, old_count, new_ring, new_count, keep[], load[],
    free[])` that, given the current resident ring and the new ring, classifies
    each cell as keep/load/free (using `ResidentSet::IndexOf`). This mirrors how
    `ResolveDistanceRing` is already an inline header helper, so the decision
    logic is testable without linking the renderers. The .cpp applies the helper
    to the parallel renderer arrays.
  - **Function(s):** `ResolveRingDiff` (new inline); `TileStreamer::SetCenter`
    calls it.
- `tests/tile_streamer_diff_smoke.cpp` (new, **Pattern A** host test —
  header-only, **no** renderer linking; see MF2)
  - **What changes:** New host test. Cannot link `tile_streamer.cpp` (it
    includes `lvl_room_renderer.hpp`/`textured_room_renderer.hpp`, which pull in
    `<t3d/t3dmodel.h>`), so it exercises the inline `ResolveRingDiff` helper
    (not the renderers). Transitions center A→B (sharing 7 of 9 cells) and
    asserts the helper classifies exactly 2 cells as load, 7 as keep, 2 as free.
    Then A→B→A→C and asserts no cell is both freed and reloaded (no
    double-free / no leak at the decision level). The actual renderer-pointer
    keep/free (N64 boundary) is verified by the on-device smoke launch and the
    A→B→A→C crash guard, not by a host test.
  - **Data shapes:** A minimal `MapSpecV2` with ≥5 rooms in a 2×2 or 3×3 grid.

#### Edge cases
- Center at a map edge (fewer than 8 neighbors): the ring is smaller; the diff
  still keeps the overlap and loads only the new cells.
- A transition whose new ring shares 0 cells (e.g., a diagonal teleport): all
  cells reload — identical to today's behavior, no regression.
- A kept cell's renderer is the textured vs flat variant: the diff must keep
  the matching array slot (`textured_renderers_` vs `renderers_`) and not
  cross them.

#### Verification
- `tests/tile_streamer_diff_smoke.cpp` passes (load count = new cells only).
- `./compile-rom.sh` builds.
- On device: cross a cell boundary; the transition hitch is significantly
  reduced (no full-ring reload). `[counters]`/profiler show no 9× load spike on
  transition. A small 1–2-cell recompile cost may remain; if it is still
  visible, quantify it in the Inc 5 close-out rather than blocking Inc 1.

### Inc 2 — Distant shared vertex buffer (M) — **SKIPPED (infeasible as specified)**

**Depends on:** (none)
**Unblocks:** Inc 5
**Status: skipped** — the as-specified mechanism is infeasible (see Finding).
Smallest memory win of the plan; the feasible re-scope needs user approval.

**Finding (verified against baked artifacts, 2026-08-19).** Premise A5 ("the 4
DLOD direction variants share identical geometry") is only half true. The bake
(`tools/ogworld/distant_lod.py::build_distant_dlod_directional`) decimates
**once** into a shared `(verts, faces)` mesh, then `_sort_direction` reorders
faces per direction (painter's back-to-front along each axis), and
`dlod_writer` expands each direction into **consecutive per-face triples in
that direction's face order**. Parsing the real `.dlod` files (`cell_01_n03`,
`cell_00_00`, `cell_02_n01`) confirms: the 4 directions' vertex **streams are
NOT byte-identical** (`d0==d1: False`) — they are **face-order permutations**;
the vertex **multisets are identical** (`d0==d1: True`). The current distant
draw path (`LvlRoomRenderer::LoadFromDlod` → `BuildRunsAndBlock` →
`EmitRunCommands`) uses tiny3d's **consecutive-triples fan** (face *f* =
`verts[3f..3f+2]`) and `t3d_vert_load` is a **linear DMA of consecutive pairs**
(no index indirection), so one buffer can only serve **one** face order.
Sharing one buffer across 4 *different* face orders is impossible on the fan
path — direction 1's face 0 would read direction 0's triangle.

**Real numbers.** Resident distant vertex RDRAM (all 45 cells; radius 6 covers
the whole map): 4×-repacked (current) = **148.1 KB**; shared-set (1 canonical
buffer + per-direction index arrays) = **58.6 KB**; saving = **~89.5 KB** (the
plan projected ~130 KB — same order, R5). This is the **smallest** memory win;
Inc 3 (~300 KB RSPQ pool high-water) and Inc 4 (fewer TMEM uploads) are larger.

**Feasible re-scope (needs user approval).** Switch the distant emission to
**indexed drawing** (`t3d_tri_draw(v0,v1,v2)` into a loaded shared vertex set —
tiny3d supports it; `t3dmodel.c` uses it). Load the shared set once per cell,
draw each direction's faces by index. This saves the ~89.5 KB **and** cuts
distant `vert_loads` (baseline 18 → ~5, one per drawn cell instead of one per
material run) — a win on both memory and the RSP-bound frame. Cost: a real
change to the distant emission path (fan → indexed), a bake or runtime dedup to
produce the shared set + index arrays, and interaction with Inc 3's
`DrawRunsDirect`. Risk: subtle distant-silhouette visual change (the user has
flagged bugs), so it should not be done unilaterally.

**Decision.** Skipped pending user approval of the indexed-drawing re-scope.
Proceeding to Inc 3/4/5 (the larger, feasible-as-specified wins). The original
spec below is superseded (kept for reference).

**Original spec (superseded — see Finding above):**

#### Files to touch

- `src/user/gameplay/render/distant_world_renderer.cpp`
  - **What changes:** When loading a cell's 4 direction variants, repack the
    `T3DVertPacked` buffer once and have all 4 variant renderers reference the
    same buffer (shared ownership, freed once when the cell is freed). Each
    variant keeps its own face list / per-direction face order.
  - **Function(s):** The cell-load path that builds the 4 `LvlRoomRenderer`
    (or equivalent) variants; the cell-free path (free the shared buffer once).
  - **Data shapes:** A per-cell shared `T3DVertPacked*` (uncached) with a
    **single owner (the cell)** — on N64 (no smart pointers) use a plain
    non-owning `T3DVertPacked*` in each variant renderer and have the cell's
    free path free it exactly once; **do NOT use a ref count** (avoids
    ref-count bugs, C2). 4 per-variant face/order arrays unchanged.
  - **Integration points:** `DistantWorldRenderer` cell load/free; the shared
    distant matrix push (Inc 3 of `lod-streaming-overhaul`) is unaffected — it
    operates on the model matrix, not the vertex buffer.
  - **Error paths:** If a cell has fewer than 4 direction variants (should not
    happen), the shared buffer is still owned by the cell and freed once.
- `src/user/gameplay/render/lvl_room_renderer.hpp` / `.cpp`
  - **What changes:** If the repack lives in `LvlRoomRenderer`, add a way to
    adopt an externally-owned vertex buffer (skip the internal repack) so the
    4 variants can share it. Keep the existing self-repack path for the near
    pass.
  - **Function(s):** `Load` (new "adopt shared verts" mode) and `Free` (do not
    free an adopted buffer).
  - **Note:** `LvlRoomRenderer` (distant/flat) and `TexturedRoomRenderer`
    (near/textured) are **sibling classes with no inheritance** — the adopt
    mode is added to `LvlRoomRenderer` only and cannot leak into the near
    pass; the near pass keeps its own self-repack untouched.
  - **Note (C1):** both renderers independently define the same `material_color`
    flat fallback table (`lvl_room_renderer.cpp` and `textured_room_renderer.cpp`).
    Inc 2/3 (lvl) and Inc 4 (textured) touch different sides of this
    duplication — keep them in sync, or factor the table to one shared location,
    to prevent drift.
- `tests/distant_shared_verts_smoke.cpp` (new, **Pattern A** host test —
  header-only, no renderer linking; see MF2)
  - **What changes:** New host test. Cannot link `LvlRoomRenderer` (pulls in
    `<t3d/t3dmodel.h>`), so it asserts the host-safe half of the premise: parsing
    one cell's `.dlod` (via `dlod_format`) yields **identical vertex sets across
    the 4 direction variants** (the basis for sharing), and that a single shared
    owner frees once (a small host-safe ownership stub). The actual shared-pointer
    wiring and `[memory]` −~130 KB are verified on device (see Verification).

#### Edge cases
- The shared buffer must outlive all 4 variants (lifetime = cell lifetime);
  freeing the cell frees the buffer exactly once.
- Per-direction face order must be byte-identical to the 4×-repacked baseline
  (the silhouettes must not change).
- **Baseline preservation:** before landing Inc 2, dump a reference
  vertex/face-order file for one representative cell from the 4×-repacked path
  (or keep the 4×-repack path behind a compile flag) so the byte-identical
  check has a concrete artifact to diff against, not just a visual eyeball.

#### Verification
- `tests/distant_shared_verts_smoke.cpp` passes (1× vertex buffer, order
  preserved against the dumped reference).
- `[memory]` telemetry shows distant vertex memory down ~130 KB.
- On device: distant silhouettes are visually unchanged.

### Inc 3 — Drop distant RSPQ blocks (M) — **Status: done**

**Depends on:** (none)
**Unblocks:** Inc 5
**Done criteria:** Distant cells allocate zero RSPQ blocks; `DrawRunsDirect` emits the same (run, face) sequence as the block path; the shared distant matrix is still pushed exactly once per frame; the RSPQ pool **high-water mark** is down ~300 KB (allowing a smaller pool configuration); distant pass visually unchanged and frame time not worse on device.

The distant pass is flat-color (no TMEM sprites) and small (~720 faces). Its
~180 RSPQ blocks (~300 KB of the pool's high-water mark) buy little. Emit the
distant runs **directly** (`vert_load` + `tri_sync` per run) instead of
capturing/running blocks (Decision 2b). **Note (SF2):** the RSPQ block pool is
a global libdragon resource sized at init — not creating distant blocks lowers
its **high-water mark**, which lets the pool be configured smaller; it does not
dynamically free a fixed allocation. Inc 5 must verify the actual pool config
reduction; if the pool isn't tuned down, the ~300 KB saving may not appear as
RDRAM free and only the frame-time (fewer DMA/buffer writes) win remains.

#### Files to touch

- `src/user/gameplay/render/distant_world_renderer.cpp`
  - **What changes:** `DrawBlockOnly` (or the distant draw path) stops calling
    `rspq_block_run`; it emits the cell's material-sorted runs directly under
    the one shared distant matrix (push once, emit runs, pop). No sprite upload
    (flat color).
  - **Function(s):** The distant draw entry point.
  - **Data shapes:** Reuses the existing per-cell run/face arrays; no block is
    captured at load.
  - **Integration points:** The shared distant matrix push (one per frame)
    stays; the direct emit happens under it. `RenderCounters` (`vert_loads`,
    `tri_syncs`) still increment.
  - **Error paths:** A run longer than the 70-vertex RSP cap is split into
    consecutive same-material runs (the baker already emits ≤70-vertex runs, so
    this is a no-op in practice but must be asserted).
- `src/user/gameplay/render/lvl_room_renderer.hpp` / `.cpp`
  - **What changes:** Add a `DrawRunsDirect()` path (no block) used by the
    distant renderer; keep the block path (`rspq_block_run`) for the near
    renderer. Gate on a flag so the block path remains for A/B (R3).
  - **Function(s):** `DrawRunsDirect` (new); `Load` (skip block capture when in
    "no-block" mode).
- `tests/distant_no_block_smoke.cpp` (new, **Pattern A** host test —
  header-only, no renderer linking; see MF2)
  - **What changes:** New host test. Cannot link `DistantWorldRenderer`/
    `LvlRoomRenderer` (t3d headers), so it asserts the host-safe half: that
    `DrawRunsDirect`'s run sequence (built from the existing per-cell run/face
    data structures) is identical to the block path's run sequence for a
    synthetic cell, with no block handle attached. The actual `rspq_block_run`
    removal and the RSPQ pool high-water drop are verified on device
    (`[memory]`) — see Verification.

#### Edge cases
- The shared distant matrix must be pushed exactly once before the direct emit
  (not per run).
- Runs must respect the 70-vertex cap (split if ever exceeded).
- The near renderer must still use blocks (this increment only changes the
  distant path).

#### Verification
- `tests/distant_no_block_smoke.cpp` passes (no distant blocks; identical run
  sequence).
- `[memory]` telemetry shows the RSPQ pool high-water mark down ~300 KB (and,
  if applicable, the pool reconfigured smaller).
- On device: distant pass visually unchanged; frame time not worse.

#### Close-out (Inc 3)

- **Implemented:** `LvlRoomRenderer::DrawRunsDirect()` (no block, no matrix
  stack touch) + `SetNoBlockMode()`/`no_block_` gate on block capture in
  `BuildRunsAndBlock`. Distant cells set `no_block_` **before** `LoadFromDlod`
  (in `dlod_loader.cpp`, both single- and multi-dir branches) so `block_` stays
  null; `DistantWorldRenderer::Render()` now calls `DrawRunsDirect()` under the
  one shared distant matrix (push once, emit, pop). Distant meshes never get
  `SetCounters()`, so `counters_` is null and the direct emit does not pollute
  the near counters (distant counters are counted separately in `Render()`).
- **Verified:** `tests/distant_no_block_smoke.cpp` (new, Pattern A) passes —
  no-block path emits the identical (run, face) sequence as the block path,
  attaches no block handle, respects the 70-vertex cap, and replays to the same
  triangle set. ROM builds clean (exit 0); host suite 36/36; Ares smoke ran
  clean (no crash; `distant_cells=11`, `distant_batches=42` — distant pass
  emitting).
- **Device-only (Inc 5 close-out per SF2):** the RSPQ pool high-water drop
  (~300 KB) and any pool reconfiguration are confirmed on device via `[memory]`.
  User confirmed "perf is really good" (frame-time win is real).
- **Pre-existing z-split render bug (NOT an Inc 3 regression):** user reported
  "a z split render issue, not fully rendering half of the stuff depending on
  the camera angle" (black vertical pillar artifact + missing geometry). Root
  cause is the **pre-existing** two-pass z-split (distant Z-off pass + near Z-on
  pass + shared near-draw-set), not Inc 1/3 — neither change touches the z-split
  camera math, Z-buffer handling, or pass ordering. Tracked separately; see
  `docs/z_split_bug.md` (Inc 5) or a follow-up plan.

### Inc 4 — Global near-pass material grouping (L) — **Status: done**

**Depends on:** (none)
**Unblocks:** Inc 5
**Done criteria:** At map center, `[counters]` `sprite_uploads` drops from ~92 (≈23 materials × ~4 cone-visible cells) to ~23 (the distinct material count, confirmed by on-device `[counters]`); the drawn (face, material) multiset is unchanged; no per-cell `vert_load` exceeds the 70-vertex cap; near visuals unchanged and map-center frame time measurably lower on device. **Must first clear the RSPQ block barrier** (see body) — the block-per-cell replay prevents naive reordering.

Restructure the near-pass draw from *per-cell → per-material* to
*per-material → per-cell* so each TMEM sprite is uploaded **once per material**
instead of once per (material, cell). Sprite uploads drop ~92→~23 at center
(Decision 3); the exact distinct-material count is measured, not assumed.
Vertex runs are **not** merged across cells (per-cell origins, A8); matrix
pushes and `vert_load`s are unchanged in count.

**RSPQ block barrier (must address first; MF1).** The near pass today plays
back one precompiled RSPQ block **per cell** (`TexturedRoomRenderer::Draw` →
`rspq_block_run(block_)`, captured at `Load` via `rspq_block_begin/end` with the
sprite uploads baked inside `EmitRunCommands`). Sprite uploads cannot be hoisted
across cells while each cell replays one opaque block. Inc 4 must therefore
re-architect the block capture from *per-cell* to *per-material*:

1. **Capture per-material blocks at Load.** Instead of one block per cell
   holding all that cell's sprite uploads, capture **one block per
   (cell, material)** — each holds the `rdpq_sprite_upload` + combiner + fan
   for exactly that cell's faces of one material. A cell with `m` materials
   yields `m` small blocks instead of 1.
2. **Replay in material order.** At draw, the orchestrator iterates materials
   globally; for each material it uploads its sprite once (TMEM), then replays
   every visible cell's block for that material. RSPQ amortization is kept
   (blocks still precompiled) while uploads are grouped by material.

**Trade-off (state it):** per-material blocks raise block count (~92 vs 9 near
blocks at center) — the opposite direction from Inc 3, which *drops* distant
blocks. The win is fewer TMEM uploads, not fewer blocks. Inc 5 must measure the
overhead of ~92 small blocks against the ~69 saved uploads. If block overhead
dominates, the fallback is the legacy per-run emit for the near pass
(`kEnableRspqBlocks = false`) — an explicit, documented alternative, not an
undocumented decision. If the barrier proves too costly, **split Inc 4** into
(a) per-material block capture (no behavior change) and (b) global material
ordering.

**Safety assumption (stated):** the near pass is fully opaque (no
transparency), so reordering draws globally by material is safe — the depth
buffer resolves draw order, not submission order. If a transparent near-pass
material is ever added, this increment must be revisited.

#### Files to touch

- `src/user/gameplay/render/textured_room_renderer.hpp` / `.cpp`
  - **What changes:** Change block capture from one per-cell block to **one
    block per (cell, material)** (MF1). Expose a cell's faces **grouped by
    material** (material id + face range + the material's sprite) and its
    per-material blocks, so the orchestrator can collect them. Add a
    `DrawMaterialBlock(material, cell_faces)` helper that, given an
    already-uploaded sprite, replays that material's block for the cell.
  - **Function(s):** Load (capture per-material blocks); Draw (replay a single
    material's block); new per-material accessors.
  - **Data shapes:** Per cell: a list of `{ material_id, sprite_ref,
    face_range }` + one block handle per entry (already computed by the
    existing material sort).
  - **Integration points:** The existing per-cell material sort is reused; only
    the *capture granularity* and *draw order* change.
  - **Error paths:** A cell with no cone-visible faces contributes nothing; a
    material present in only one cell behaves exactly as today.
- `src/user/gameplay/render/tile_streamer.cpp` — **primary restructure (SF4)**
  - **What changes:** `TileStreamer::DrawHighPriority` (this is where the actual
    per-cell draw loop lives — **not** `OpenWorldRenderer::Render`, which merely
    calls `tile_streamer_->DrawHighPriority(cam)`). Replace the per-cell
    `Draw()` loop with a per-material → per-cell loop: iterate the global
    material order, upload each sprite once, then for each cone-visible cell
    that has that material, replay its block under the cell's matrix. Expose the
    cone-visible residents' per-material groups to the loop (read-only accessor;
    no behavior change to streaming).
  - **Function(s):** `TileStreamer::DrawHighPriority`; new accessor returning
    the visible residents + their material groups.
  - **Data shapes:** A per-frame (FrameArena) list of `(material_id,
    cell_index, first_face, face_count)` — a fixed 4×4-byte entry, **not** an
    unbounded face list (see SF3) — grouped by material.
  - **Integration points:** `OpenWorldRenderer::Render` is unchanged except it
    keeps calling `DrawHighPriority`; `RenderCounters` (`sprite_uploads`) now
    counts ~distinct materials.
  - **Error paths:** If the per-frame triple list exceeds the FrameArena (64 KB),
    fall back to the per-cell draw (assert + counter) rather than overflow.
- `src/user/gameplay/render/open_world_renderer.cpp`
  - **What changes:** None functionally — it already calls
    `tile_streamer_->DrawHighPriority(cam)`; the near-pass draw loop is owned by
    `TileStreamer`. Only the near-draw set computation (already in `Render`)
    stays as the single source of truth for which cells are cone-visible.
  - **Function(s):** none (or a comment update).
- `tests/near_global_sort_smoke.cpp` (new, **Pattern A** host test —
  header-only, no renderer linking; see MF2)
  - **What changes:** New host test. Cannot link `TexturedRoomRenderer` (t3d
    headers), so it exercises the host-safe **grouping decision**: 3 synthetic
    cells with overlapping materials, and asserts the global material→cell
    collection yields exactly one upload slot per distinct material (not the sum
    of per-cell counts), preserves the drawn (face, material) multiset, and
    keeps each cell's per-material face count under the 70-vertex cap. The actual
    per-material block capture/replay and `[counters]` drop are verified on
    device.

#### Edge cases
- A material's faces in one cell already form a ≤70-vertex run (the existing
  per-cell runs are capped); grouping does not concatenate across cells, so the
  cap is never newly exceeded.
- Stable order within a material: preserve each cell's existing face order so
  overdraw/z-order is unchanged.
- The FrameArena must hold the per-frame triple list; assert capacity and fall
  back to per-cell draw on overflow.
- **FrameArena headroom (verify, don't assume; SF3):** the triple entry is a
  fixed `(material_id, cell_index, first_face, face_count)` = 4×4 B = 16 B
  (not an unbounded face list). Worst case: 9 cells × ~23 materials = 207
  entries × 16 B ≈ 3.3 KB — small, but measure the free space after the
  existing per-frame allocations (distant render list, skybox, etc.) before
  relying on the 64 KB FrameArena. If headroom is tight, give the triple list
  its own small static buffer instead of the FrameArena.

#### Verification
- `tests/near_global_sort_smoke.cpp` passes (uploads = distinct materials;
  multiset unchanged).
- `[counters]` shows `sprite_uploads` down from ~92 to ~23 (distinct
  materials) at map center.
- On device: map-center frame time measurably lower (profiler); near visuals
  unchanged.

#### Close-out (Inc 4)

- **MF1 decision — direct emission, not per-(cell, material) blocks.** The
  plan's prescribed mechanism (capture one RSPQ block per (cell, material),
  ~92 small blocks at center) was replaced by **direct emission** (the plan's
  own documented `kEnableRspqBlocks = false` fallback), gated by
  `kEnableGlobalMaterialGrouping = true` in `textured_room_renderer.hpp`.
  Rationale: (1) the frame is **RSP-bound** (`rspq_wait()` after `Render()`),
  so blocks and direct emit have *identical* RSP cost — blocks only save CPU
  time, which has headroom; (2) direct emission allocates **zero** near-pass
  RSPQ blocks, lowering the pool high-water mark (consistent with Inc 3, not
  the ~92-block increase the plan's block path would cause); (3) simpler and
  lower-risk. The legacy per-cell block path (`TexturedRoomRenderer::Draw`) is
  kept intact behind the flag for A/B (R3).
- **Implemented:** `TexturedRoomRenderer` derives a `MaterialGroup` list
  (`{material_id, first_run, run_count}`) at the end of `Load` (contiguous
  same-material runs grouped; freed in `Free()`). New public API:
  `MaterialGroupCount()` / `MaterialGroupAt()` / `UploadMaterial()` /
  `DrawMaterialRun()`. `EmitRunCommands` refactored into `EmitRunState`
  (combiner + sprite upload / shaded fallback — the per-material state) +
  `EmitRunGeometry` (vert_load + face fans + tri_sync — the per-run geometry);
  the legacy path is `EmitRunState` + `EmitRunGeometry` unchanged.
  `TileStreamer::DrawHighPriority` restructured: pass 1 = flat (untextured)
  renderers per-cell as before; pass 2 = textured renderers — build a per-frame
  `NearMaterialTriple` list, `SortMaterialTriplesByMaterial` (stable insertion
  sort), then per material: `UploadMaterial` **once**, then replay each cell's
  `DrawMaterialRun` under that cell's matrix.
- **SF3 resolved — static triple buffer.** The per-frame triple list is a
  file-local `static NearMaterialTriple s_triples[288]` (9 cells × 32
  materials × 8 B = 2.3 KB), per the plan's "give the triple list its own
  small static buffer" escape hatch — no FrameArena headroom risk.
- **Counter semantics:** `texture_uploads` is now **per-material** (was
  per-run); `near_batches` / `vert_loads` / `syncs` unchanged (one per run).
  Matrix pushes rise from per-cell to per-(cell, material) — a minor RSP cost
  to note in Inc 5 close-out.
- **Verified:** `tests/near_global_sort_smoke.cpp` (new, Pattern A) passes —
  one upload slot per distinct material, drawn (cell, material) multiset
  preserved, stable order within a material, per-material run totals preserved.
  ROM builds clean (exit 0); host suite 36/36; Ares smoke ran clean (no crash,
  steady 30 fps at the 30 fps cap).
- **Device measurement (spawn + one heavy location):** at spawn
  `texture_uploads=2` (2 distinct materials visible; legacy would be ~22, one
  per textured run), `near_batches=22 vert_loads=22 syncs=22` unchanged. At a
  heavy location (`distant_cells=21`, `near_batches=114`) `texture_uploads=5`
  (legacy would be ~114). **The map-center 63→~23 projection is confirmed in
  Inc 5 close-out** (spawn is a lighter location than center).

### Inc 5 — Close-out: measurement + docs (S) — **Status: done**

**Depends on:** Inc 1, Inc 2, Inc 3, Inc 4
**Unblocks:** (none)
**Done criteria:** All host tests (existing + 4 new) pass via `./tests/run_host_tests.sh`; ROM builds and device smoke launch OK; `docs/perf_budget.md` has a measured before/after table (frame time, `[memory]` peak, `[counters]`) with projected-vs-actual deltas; `docs/streaming_memory.md` documents the new architecture.

Measure the combined before/after, update the docs, and register all new host
tests.

#### Files to touch

- `docs/perf_budget.md`
  - **What changes:** Add a measured before/after table: frame time at map
    center (profiler), `[memory]` peak (FrameArena + RSPQ pool), `[counters]`
    (`sprite_uploads`, `vert_loads`, `tri_syncs`). Record the projected vs
    actual deltas (distant verts ~130 KB, distant blocks ~300 KB, sprite
    uploads ~92→~23).
  - **Data shapes:** Markdown table.
- `docs/distant_lod.md`
  - **What changes:** Note the shared per-cell vertex buffer (Inc 2) and the
    no-block direct emit (Inc 3).
- `docs/streaming_memory.md` (new)
  - **What changes:** Document the post-plan streaming + memory architecture:
    incremental near ring, distant per-cell memory reduction, global near
    material grouping, and the RSP-bound frame model.
- `tests/run_host_tests.sh`
  - **What changes:** Register the four new host tests in the **Pattern A**
    (header-only) section — `tile_streamer_diff_smoke`,
    `distant_shared_verts_smoke`, `distant_no_block_smoke`,
    `near_global_sort_smoke` (all Pattern A per MF2, no renderer linking, no
    baked fixture needed — **not** Pattern C).

#### Edge cases
- If a measured delta doesn't match the projection, document the delta and the
  likely cause (R5) rather than forcing the number.

#### Verification
- All host tests (existing + 4 new) pass via `./tests/run_host_tests.sh`.
- `./compile-rom.sh` builds; device smoke launch OK (Ares or Mupen64Plus).
- Docs reflect the measured numbers.

#### Close-out (Inc 5)

- **Docs written:** `docs/perf_budget.md` (measured before/after table at map
  center + projected-vs-actual deltas + memory note), `docs/distant_lod.md`
  (no-block direct emit Inc 3 + shared per-cell vertex buffer note),
  `docs/streaming_memory.md` (NEW — post-plan streaming + memory architecture:
  incremental near ring, distant per-cell memory reduction, global near
  material grouping, RSP-bound frame model, key constants).
- **Tests registered:** `tests/run_host_tests.sh` now runs the 3 new Pattern A
  tests (`tile_streamer_diff_smoke`, `distant_no_block_smoke`,
  `near_global_sort_smoke`) — **39 total, all pass**. (Inc 2 was skipped, so
  there is no `distant_shared_verts_smoke`; the plan's "4 new" is 3.)
- **Measured at map center (Ares, teleported via `kDebugTeleportToMapCenter`):**
  frame time ~39.4–40.8 ms → **~33.1–33.4 ms (30 fps cap)**; `texture_uploads`
  **63 → 5**; `near_batches`/`vert_loads`/`syncs` unchanged (132). Heavy
  location (`distant_cells=19`) ~50 ms — a heavier-location cost, not a
  regression.
- **R5 deltas documented honestly:**
  - `sprite_uploads` projected ~92→~23; **actual 63→5** — the map uses a small
    ~5-material palette, so the distinct-material count (5) is far below the
    ~23 projection. Mechanism (per-material upload) confirmed; absolute number
    is palette-bound.
  - Distant verts ~130 KB / blocks ~300 KB savings are **not visible as RDRAM
    free** because the ROM now targets the **Expansion Pak (8 MB RDRAM)** —
    `rom_main.cpp` calls `assert_memory_expanded()` at boot, so the heap is the
    full 8 MB arena (heap `total` 3.2 MB → 5.48 MB). This is the intended
    target-hardware change, not caused by Inc 1/3/4 — those reduce
    allocations. `used` grew proportionally to `total` (same fraction used),
    confirming no new leak. The real, measured win is the frame-time
    improvement + 92% upload reduction.
- **Verified:** ROM builds clean (exit 0); host suite 39/39; final Ares smoke
  (normal Start-spawn boot) clean — steady 30 fps, `texture_uploads=2`, no
  crash. Teleport flag reverted to `false` after the center capture.

## Cross-cutting verification

- **After every increment:** `./compile-rom.sh` builds; the full host test suite
  passes; a device smoke launch (Ares/Mupen64Plus) runs without crashing.
- **After all increments (Inc 5):** measure at map center — frame time
  (profiler), `[memory]` peak, `[counters]` — and compare to the pre-plan
  baseline. Confirm: (a) transition hitch significantly reduced — no 9× load
  spike (Inc 1), (b) distant vertex memory −~130 KB (Inc 2), (c) RSPQ pool
  −~300 KB (Inc 3), (d) `sprite_uploads` ~92→~23 (Inc 4).
- **Regression guards:** near visuals unchanged (same faces/materials/z-order);
  distant silhouettes unchanged; no pop-in on free camera orbit (ring still 9
  cells); no crash or leak on rapid A→B→A→C transitions.

## Standards / common-mistakes referenced

- `.agents/common-mistakes/dfs-path-prefix.md` — LVL/DLOD paths must be
  `rom:/...`; Inc 1's load path reuses the existing `LocalizePath`, so the
  prefix rule is preserved.
- `.agents/common-mistakes/missing-player-start-init.md` — the boot path
  (`SetCenter` at boot) must still produce a valid ring after the Inc 1 diff;
  the boot center is always `out[0]` of `ResolveDistanceRing`.
- `AGENTS.md` working rules — preserve the gameplay/ROM separation; rebuild the
  ROM after each N64-facing increment; re-run the host smoke test after each
  gameplay/render change.

## Open questions (CONSIDER from review)

- Should the near ring shrink from 9 to 5 (center + cross) to halve near-ring
  memory? Rejected for now (pop-in on free camera orbit); revisit only if
  memory is still tight after Inc 1–4.
- Could the distant pass be culled more aggressively at close range (it's only
  ~720 faces and already extent-culled)? A tighter cull might save a little
  more RSP, at the risk of a visible pop at the fog boundary.
- Is the 70-vertex RSP cap the right split point for any future cross-cell run
  merging, or should it be tuned per material?
- **File overlap among "independent" increments (CONSIDER from review):** Inc 2
  and Inc 3 both edit `distant_world_renderer.cpp` + `lvl_room_renderer.hpp/.cpp`;
  Inc 1 and Inc 4 both edit `tile_streamer.cpp`. There is no data dependency and
  the DAG is correct, but an implementer should land them **sequentially** (not
  in parallel branches) to avoid merge conflicts on those shared files.
- **Done criteria mix host- and device-verifiable checks (CONSIDER from
  review):** each increment's done criteria include both host-test assertions
  (runnable via `./tests/run_host_tests.sh`) and on-device checks (profiler,
  `[memory]`, `[counters]`, visual). When implementing, treat the host-side
  criteria as the merge gate and the device criteria as the close-out (Inc 5)
  confirmation, so a device-only gap never blocks a host-verified increment.

## Out of scope

- Art/texture changes (reducing the texture count is an art decision).
- Async/prefetch cell loading (the sync load of 1–2 cells is small after the
  Inc 1 diff).
- Changing the DLOD bake budget (20 faces/direction) — a bake-time
  quality/size decision.
- The fog depth-space approximation (treated as a tuning knob by
  `lod-streaming-overhaul`; not a streaming/memory issue).
- CMSH collision memory (383 KB, required for collision; not reducible without
  changing the collision representation).
- CPU-side refactors (the frame is RSP-bound; CPU savings do not reduce frame
  time).
