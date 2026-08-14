# LOD Streaming Overhaul (distant pop/hole/edge fixes + dynamic residency)

## Context

The compressed distant-LOD work (`.plans/compressed-distant-lod/`) shipped a
working distant tier, but device review shows it is "not where it should be":

1. **Popping at direction boundaries** — each cell bakes 4 **different**
   geometry sets (`_direction_silhouette` keeps only faces facing each
   direction). Switching directions swaps the whole mesh → visible pop at
   every 45° line.
2. **Holes / gaps in the horizon** — a face ~46° from both adjacent direction
   normals is kept by neither; oblique views show missing patches.
3. **Visible square textured→flat edge** — the near ring is a 3×3 *square*
   (corner cells up to ~508u from the camera) while the distant near plane is
   360u *radial*; ring-corner cells are drawn by **both** passes and the
   flat/tex transition follows the square ring boundary — and it sits at
   **~0% fog** (fog onset 532u), so it is fully visible.
4. **Horizon pop-in at screen edges** — `CellInDistantFrustum` tests the cell
   *center* against the view cone; a 240u cell whose center is just off-screen
   is culled even though part of it is visible.
5. **Still not 30 fps** — per-frame matrix rebuild storm (up to 180
   `t3d_mat4_to_fixed`/frame via `SetCameraPosition`) + per-cell matrix
   push/pop + no cross-cell draw-state dedup.
6. **Latent double-free** — `FreeEntries()` frees each non-null `meshes[d]`
   slot, but the single-direction `.dlod` path puts the **same** pointer in
   all 4 slots → double-free/use-after-free on reload/`SetCenter`
   (`--no-directional` fallback). Live crash on the documented fallback.

The reference `lambertjamesd/n64brew2025` (2025 Brew Jam, libdragon preview +
tiny3d) solves these. Its distant LOD1 pass: **one shared camera-relative
matrix for the whole pass** (≤2 pushes), **the same geometry baked 4× with
per-direction triangle *sort* order** (painter's algorithm, Z off — no geometry
swap, no pop), **extent-aware 2D clip-plane culling** (`CULL_TOLERANCE` scaled
by `lod_scale`), an **LOD2-overlap skip** (`LEVEL2_MIN_DISTANCE`), and
**streamed tile residency** (wrap-around slots + `load_next`). This plan ports
those patterns to the two-pass open-world renderer.

**User-confirmed scope:** full pass overhaul; the distant tier becomes
**dynamically streamed** (region-based load/unload driven by the camera cell)
instead of all-45-cells-resident (reverses the compressed-LOD plan's D4); far
assets are optimized (same-geometry painter-sorted variants, shared matrix,
extent culling, overlap fix).

**Intended outcome:** no popping, no holes, no ring-edge double-draw (the
flat→tex boundary is softened by fog), no screen-edge pop-in, stable 30 fps in
the whole-map view, and a distant tier whose resident memory scales with
proximity (not map size) so larger maps stay viable.

## Architectural decisions

- **D1 — Same-geometry, painter-sorted direction variants (n64brew2025 style).**
  Decimate each cell **once** to the per-cell budget, then emit 4 direction
  sections that reference the **same** triangles reordered back-to-front along
  each direction axis. Because the runtime's `SortFacesByMaterial` is a
  *stable* sort, the bake orders each direction as **(material_id, centroid
  distance along the axis)** so material runs still form AND within-material
  painter order survives. Fixes popping + holes with zero runtime geometry
  machinery changes. Alternatives rejected: keep per-direction silhouettes +
  hysteresis (holes remain); single 360° mesh (no per-direction painter order —
  kept as the `--no-directional` fallback knob).
- **D2 — One shared camera-relative matrix for the whole distant pass.** Pack
  all distant verts relative to a **single shared world origin** (map center)
  instead of per-cell origins. At `kLodScale = 0.25` the full map diagonal
  (~2000u) packs to ~500 int16 units — far inside range, so per-cell origins
  were unnecessary conservatism for the distant pass (they remain required for
  the near pass at `kPosScale = 32`). Runtime: one `T3DMat4FP` rebuilt once per
  frame (or on camera move); the pass pushes it once, runs every cell's block,
  pops once. Kills ~180 matrix rebuilds/frame and per-cell push/pop. The DLOD
  header `version` bumps to 2 so a stale per-cell-origin v1 `.dlod` **fails to
  parse** (cell skipped) instead of silently misrendering.
- **D3 — Extent-aware distant culling.** A cell is culled only when its whole
  XZ AABB is outside the camera cone + depth range. Per-corner angular test
  widened by `atan(cell_half_diagonal / dist)` (so the cone cannot pass through
  the cell interior with all 4 corners outside), plus near/far depth slack
  scaled by cell extent. Fixes screen-edge pop-in. Aligned with the existing
  `kCullMargin` constant.
- **D4 — Overlap handoff + near cone-cull; the square edge is softened, not
  eliminated (memory-bounded).** The near pass draws a **resident** cell iff
  its AABB intersects the camera cone (no grid-index cut); the distant pass
  skips exactly the cells the near pass will draw (one shared, host-safe
  `near-draw set` predicate computed once per frame). This removes the
  double-draw band and the mid-cell cut. The **textured→flat boundary *shape*
  remains the 3×3 ring outline** — textured coverage out to the fog onset
  (532u) would need a 5×5 resident ring (25 textured cells ≈ 500 KB, over the
  near budget), so it is explicitly out of scope. The edge is hidden instead by
  **fog softening**: the distant near plane moves to the ring far edge
  (~1.5 × cell × √2 ≈ 508u) so there is no gap/overlap, and fog onset is
  lowered (~0.28 × sqrt(kDistantMaxDist2) ≈ 370u) so the ring boundary sits
  inside the fog ramp. Result: no double-draw, no mid-cell cut, and a softened
  (fogged) transition; the boundary shape stays square within the 9-cell
  budget (documented, user-visible decision).
- **D5 — Dynamic distant residency (streaming).** The distant tier loads cells
  within `kDistantStreamRadius` (Chebyshev, default **6** = 1440u) of the
  camera cell and evicts cells outside it, freeing all direction meshes. The
  radius is derived so the **worst-case load distance** (radius × cell −
  half-cell, because distance tests hit the cell center) stays ≥ the
  fog-complete distance: `6·240 − 120 = 1320u > 1197u` — a cell is always fully
  fogged before it can become drawable, so eviction/load is invisible. Honest
  note: on this 45-cell map the whole map stays resident at center (radius 6
  clamps to the map), so the *memory* win is minimal here; the deliverable is
  the architecture (boot loads a smaller initial set; larger maps scale) and it
  is the user-requested "dynamically reload assets".
- **D6 — Keep Z-off back-to-front distant order; cross-cell dedup is
  measurement-gated.** The distant pass stays Z-off, distance-primary
  back-to-front (far first). Sorting by material would break cross-cell painter
  order (a far cell could overpaint a nearer one along a ray) — so the
  `dominant_material` grouping is **dropped** as a committed sort. Inc 7
  measures whether prim-color churn matters; if device profiling shows it does,
  the follow-up is a **clean A/B**: (A) today's Z-off + distance-primary sort
  vs (B) Z-on + material grouping. Note that enabling a Z test **invalidates
  the distance-sort rationale** — with Z on, the RDP resolves order, so the
  back-to-front sort is no longer needed and material grouping becomes safe.
  The experiment is documented, not committed.
- **D7 — Two-pass architecture, fog/drop coupling, near ring residency stay.**
  Distant Z-off back-to-front, near Z-on textured; `kDistantMaxDist2` and the
  drop stay unchanged; fog **onset ratio** is lowered (~0.28 instead of 0.4)
  per D4 so the ring boundary is fogged — the invariant "fog completes before
  the drop" still holds (onset ~370u, complete ~1197u, drop ~1330u). **The
  fog-onset ratio is the single tuning point for the ring boundary** — it is
  the one knob that controls where the flat→tex transition sits relative to
  the fog ramp; all other ring-boundary tuning (near plane, residency) is
  fixed by the 9-cell budget. Near *residency* stays 3×3 (now cone-culled by
  D4). `kDistantStreamRadius` must satisfy the D5 invariant
  (`≥ ceil(fog_complete/cell + 0.5)`).

## Assumptions and answers from code

- Near ring residency = center + Chebyshev-1 (`kMaxRing = 9`) —
  `tile_streamer.hpp:31-34, 41-63` (`ResolveDistanceRing`). The ring is a
  *residency* pool; D4 changes what it **draws**, not what it holds.
- Distant near = `1.5 × tile_size = 360u` radial today; drop =
  `sqrt(kDistantMaxDist2) ≈ 1330u`; fog `0.4→0.9` ≈ 532→1197 —
  `pass_camera_math.hpp:54-71`, `lod_math.hpp:44-57`.
- `DirectionalMeshIndex`/`DirectionalIndexFromDelta` host-safe and correct —
  `lod_math.hpp:104-140`. Direction indexing matches `_DIRECTION_NORMALS`
  (`0=+Z,1=-Z,2=+X,3=-X`) — `tools/ogworld/distant_lod.py:555-566`.
- `_direction_silhouette` is the per-direction geometry extractor (root of
  popping/holes) — `distant_lod.py:570-591`.
- `LoadDistantCellDlodAll` shares slot 0 across all 4 slots for a single-dir
  `.dlod` — `dlod_loader.cpp:104-113`; `FreeEntries` frees each non-null slot →
  double-free on the shared path — `distant_world_renderer.cpp:35-49`.
- `SetCameraPosition` rebuilds a full `T3DMat4FP` per mesh every frame (up to
  180) — `distant_world_renderer.cpp:120-132`, `open_world_renderer.cpp:157-161`.
- Per-cell `Draw()` = push `matrix_fp_` + `rspq_block_run(block_)` + pop —
  `lvl_room_renderer.cpp:509-546`. The block contains prim color per material
  run; `SortFacesByMaterial` is a **stable** sort — `batch_coalesce.hpp`.
- Bake packs verts relative to the passed `origin` at `kLodScale`; the DLOD
  header stores that origin — `tools/writers/dlod_writer.py`, `docs/distant_lod.md`.
- `ResolveVisibleTiles`/`ScanlineTileRanges` exist but unused; the near pass
  draws all 9 residents — `tile_streamer.hpp:67-88`, `tile_streamer.cpp:196-222`.
  The old grid-index gate was removed because it cut geometry at cell
  boundaries during rotation — D4's AABB-cone test avoids that class.
- Bake origin today = per-cell center (`render_origin`) —
  `bake_interconnected_map.py:194-213`.
- Map: 45 cells, cell = `1200 × 0.2 = 240u`, map ≈ 6.3 × 5.7 cells
  (~1510 × 1370u), drop 1330u ≈ 5.5 cells. Chebyshev radius from any interior
  cell ≤ ~3, so radius 6 keeps the whole map resident.
- Makefile: DFS wildcard ships `*.dlod`; bake rule depends on
  `bake_interconnected_map.py` + `distant_lod.py` + `dlod_writer.py` —
  `Makefile:60-65, 112-137`. A stale `.dlod` is re-baked automatically on
  `make`; the DLOD version bump (D2) makes any leftover v1 file fail loudly.
- Host tests: Pattern A (`run_cpp`) + Python contracts (`run_py`) in
  `tests/run_host_tests.sh` — an **explicit list**; every new contract must be
  wired at its own increment, not deferred.
- User-confirmed: full-pass overhaul, dynamic streaming, all five observed
  symptoms, device validation in an emulator with the `[counters]`/`[memory]`
  serial report.

## Risks accepted

- **Streaming pop-in at the radius edge**: eliminated by construction — radius
  6 ⇒ worst-case load distance 1320u > fog-complete 1197u (D5 invariant,
  contract-asserted). The residual risk is a *hitch* on load/evict, mitigated by
  tiny cells (~0.7 KB `.dlod`) and loads happening in `SetCenter` (transition),
  not per frame.
- **Within-cell painter order errors with Z off**: the bake orders faces
  (material, dist-along-axis); inter-material overlap is rare for flat-color
  cells and the previous single-mesh form already validated this look. Accept;
  revisit only if device shows visible order artifacts.
- **Shared-origin packing precision**: 0.25 × ~2000u diagonal ≈ 500 int16 —
  comfortable, asserted by a host contract. **Per-map ceiling** documented:
  `map_diagonal × kLodScale ≤ ~28000` (see Inc 3 + `docs/distant_lod.md`).
- **Streaming on this map saves little memory**: honest — at map center all 45
  cells are within radius 6 and stay resident (~45 × 4 × 20 faces ≈
  150-200 KB distant resident worst case, still under the ~300 KB target). The
  deliverable is the architecture + the smaller boot set; real memory wins come
  with larger maps or a future raised drop distance.
- **Per-cell 4× geometry resident (D1)**: 4 blocks/cell × 20 faces — bounded by
  the stream radius (whole map worst case). Measured in Inc 7.
- **DLOD v1 keeps duplicated verts across directions** (no v2 "shared verts"
  section): on-disk ~4× vert bytes (still ~100 KB total for 45 cells). Format
  churn rejected; a future v2 can share verts. (The v2 bump in Inc 3 is the
  **version field only** — same layout semantics, origin now shared.)
- **Ring grid-snap at cell crossings**: the near *draw* decision is cone-based
  (AABB-cone), so a cell is no longer cut mid-cell when it crosses a cell
  boundary; however the textured→flat boundary still follows the ring outline
  (see D4) — softened by fog, not eliminated. Documented as a known artifact
  within the 9-cell residency budget.

## Increment DAG

- Inc 1 — Far-bake: same-geometry painter-sorted variants (L) — depends: none — unblocks: 3, 6
- Inc 2 — Double-free fix (S) — depends: none — unblocks: 6 (defense-in-depth) — parallel, ships immediately
- Inc 3 — Shared distant pass matrix + DLOD v2 (M) — depends: 1 — unblocks: 6, 7
- Inc 4 — Extent-aware distant culling (M) — depends: none — unblocks: 5
- Inc 5 — Ring/distant overlap + camera-relative near coverage (L) — depends: 4 — unblocks: 6
- Inc 6 — Dynamic distant streaming (M) — depends: 1, 3, 5 — unblocks: 7
- Inc 7 — Close-out: measurement, docs, device tuning (S) — depends: 2, 3, 6 — unblocks: —

```
Inc1 ──► Inc3 ──► Inc6 ──► Inc7
  \        ▲      ▲
   \       │  ┌───┘
Inc4 ──► Inc5 ─┘

Inc2 (parallel — ships immediately, no deps)
```

Two parallel tracks (bake/runtime-matrix vs culling), merged at Inc 6. Inc 1→3
serial: Inc 3's shared-origin bake change edits the same
`build_distant_dlod*` functions Inc 1 rewrites and both re-bake — serializing
removes merge/rebake churn. Inc 4→5 serial (the overlap predicate builds on the
extent-cull math). Inc 6 needs Inc 1 (4-sort bake), Inc 3 (shared matrix makes
streamed reloads cheap + v2 fail-loud), Inc 5 (coverage regions). Inc 2 is
independent and can land first. Inc 7 is the measurement/close-out.

## Increments

### Inc 1 — Far-bake: same-geometry painter-sorted direction variants (L)
**Status:** done
**Depends on:** none
**Unblocks:** 3, 6
**Done criteria:** a fresh bake emits 4-direction `.dlod` where all 4 directions
contain the **same triangle set** (same verts/faces, only order differs —
sorted by `(material_id, centroid distance along the direction axis)`); the
Python contract asserts geometry-equivalence across directions and the order
invariant; per-direction face count ≤ 20; `--no-directional` single-mesh
fallback preserved. Device: a slow 360° turn shows no popping and no holes.

#### Files to touch

##### tools/ogworld/distant_lod.py
- What changes: replace the per-direction geometry extraction with a single
  decimation + per-direction reorder.
- Function(s):
  - `_sort_direction(faces, verts, dir_index) -> List[tuple]` (new): return
    `faces` reordered by `(material_id, dot(centroid, _DIRECTION_NORMALS[dir_index]))`
    ascending (centroid = mean of the face's 3 verts). Stable — ties keep bake
    order.
  - `build_distant_dlod_directional(chunk, material_count, origin, out_path,
    lod_scale, budget, dir_budget)`: decimate **once** via `build_distant_lod(
    polygons, lod_scale, budget=dir_budget)` (single 360° mesh, budget = the
    per-direction budget, default 20), then build 4 directions as
    `(verts, _sort_direction(faces, verts, d))` for d in 0..3. Same `verts`
    object passed to all 4 (the writer packs per-face triples, so this is
    still 4× on-disk verts under DLOD v1 — accepted, see Risks).
  - Remove `_direction_silhouette` (dead).
- Data shapes: unchanged `(verts, faces)` contract; directions share `verts`.
- Integration points: `bake_interconnected_map.py` calls
  `build_distant_dlod_directional` unchanged; `--no-directional` still routes
  to `build_distant_dlod` (single mesh).
- Error paths: a cell with no geometry → None (unchanged); empty direction
  impossible now (all directions share the same non-empty face set).

##### tools/writers/dlod_writer.py
- What changes: none (already writes `(verts, faces)` per direction).
- Error paths: unchanged.

##### tests/distant_decimation_contract.py
- What changes: extend with the direction-equivalence contract.
- Asserts:
  - **geometry equivalence**: for a synthetic cell, the 4 directions decode to
    the **same** triangle set (as 3-point coordinate sets, order-insensitive).
  - **order invariant**: each direction's face list is sorted by
    `(material_id, dot(centroid, dir_normal))` ascending.
  - **no holes**: every source face appears in every direction.
  - face count per direction ≤ 20 (updated from 12).
- **Wire into `tests/run_host_tests.sh` at this increment** (explicit list).

##### tests/dlod_format_contract.py
- What changes: the 4-direction assertions update the per-direction face
  budget 12 → 20.
- Error paths: none.

#### Edge cases
- A cell whose decimated mesh has 0 faces → all 4 directions empty → skipped
  (existing behavior).
- Ties in the sort (coplanar faces at the same axis distance) → stable, keep
  material-grouped bake order.
- The load-time stable material sort must preserve the within-material painter
  order — the bake emits material-contiguous order so this holds; the contract
  pins it.

#### Verification
- Run: `./tests/run_host_tests.sh` green (new/extended contracts, wired here);
  re-bake (`make bake-forsaken-city` or the documented `bake_interconnected_map.py`
  command); decode the published `.dlod` → 4 directions share geometry.
- Device (user): slow 360° turn in the whole-map view — no popping at
  direction boundaries, no horizon holes.
- Done: contracts green, bake emits equivalent directions, device shows no
  pop/holes.

### Inc 2 — Double-free fix (S)
**Status:** done
**Depends on:** none
**Unblocks:** 6 (defense-in-depth); ships independently/immediately
**Done criteria:** `FreeEntries()` frees each distinct mesh pointer exactly
once per entry (shared-slot single-dir cells no longer double-free); a
host-testable distinct-pointer helper is covered by a Pattern A test; repeated
reload cycles (when they exist today) hold memory stable. No streaming yet —
this is the crash fix standing alone.

#### Files to touch

##### src/user/gameplay/render/distant_world_renderer.{hpp,cpp}
- What changes: `FreeEntries()` dedupes distinct `LvlRoomRenderer*` per entry
  before freeing (an entry whose 4 slots share one pointer must free it once).
- Function(s):
  - `static int CollectDistinctMeshes(const DistantLodEntry&, LvlRoomRenderer*
    out[4])` — host-safe, returns the distinct pointer count.
  - `FreeEntries()` — loop entries, collect distinct, `Free()` + `delete` each
    once, null all slots.
- Data shapes: none new (helper only).
- Integration points: destructor + reload path (existing).
- Error paths: idempotent (null slots safe).

##### src/user/gameplay/render/dlod_loader.cpp
- What changes: **none here.** The stop-sharing change (single-dir loads slot 0
  only, slots 1..3 null) lands in **Inc 3**, which already edits
  `LoadDistantCellDlodAll` for the shared origin. This increment touches only
  `FreeEntries`/`CollectDistinctMeshes` so it is merge-clean and CI-green on its
  own; until Inc 3, the dedupe handles the shared-pointer path correctly.
- Error paths: unchanged.

##### tests
- New Pattern A: `distant_dedup_contract.cpp` — `CollectDistinctMeshes` returns
  1 for a shared-slot entry, 4 for distinct; `FreeEntries`-style loop frees
  each distinct pointer once. **Host-safe trick:** the helper only compares
  pointers (never dereferences), so the test passes fake pointer values
  (`reinterpret_cast<LvlRoomRenderer*>(0x1...)`) — it must NOT instantiate the
  N64 `LvlRoomRenderer` on host.
- **Wire into `tests/run_host_tests.sh` at this increment.**

#### Edge cases
- Entry with all slots null → nothing to free.
- Mixed entries (some shared, some distinct) → each distinct pointer freed once.
- The dedupe must not change the destructor path's behavior for the current
  4-distinct bake (frees 4, as today).

#### Verification
- Run: `./tests/run_host_tests.sh` green (new contract); `./compile-rom.sh`
  clean (header change).
- Device (user): boot + one reload/transition — no crash, no memory creep.
- Done: double-free fixed, dedupe contract green.

### Inc 3 — Shared distant pass matrix + DLOD v2 (M)
**Status:** done
**Depends on:** 1
**Unblocks:** 6, 7
**Done criteria:** all distant cells pack relative to **one shared world origin**
(map center); the distant pass pushes a single camera-relative `T3DMat4FP` once
per frame and draws every cell's block under it (no per-cell push/pop, no
per-mesh rebuild); the DLOD version is 2 and a stale v1 `.dlod` fails to parse
(cell skipped, no misrender); a host contract asserts the full-map packing stays
inside int16; device: `[counters]` shows the distant draw with ≤2 matrix
pushes and visual output unchanged.

#### Files to touch

##### tools/bake_interconnected_map.py
- What changes: compute a **shared** world origin (map AABB center, world
  space) once and pass it to `build_distant_dlod_directional` /
  `build_distant_dlod` for **every** cell, instead of the per-cell
  `render_origin`. **Bump the DLOD version to 2** via the writer.
- Function(s): the per-cell bake loop (`main()`), `:194-213`.
- Data shapes: `origin` becomes the shared map-center Vec3.
- Error paths: none (bake-side).

##### tools/writers/dlod_writer.py
- What changes: header `version` field = 2 (the layout is otherwise unchanged;
  the semantic change is "origin is now the shared map center").
- Error paths: unchanged.

##### tools/ogworld/distant_lod.py
- What changes: no logic change — `build_distant_dlod*` already take `origin`;
  the writer packs `(world - origin) * lod_scale`. The DLOD header `origin`
  now carries the shared origin for all cells.
- Error paths: none.

##### src/user/gameplay/render/dlod_format.hpp
- What changes: accept version 2 (strict — v1 returns -1 so a stale per-cell
  `.dlod` fails loudly, cell skipped). Update the magic/version asserts + the
  doc comment.
- Function(s): `ParseDlod` (version check 1 → 2).
- Error paths: version != 2 → -1 (fail loud, cell skipped).

##### src/user/gameplay/render/dlod_loader.cpp
- What changes: (a) `LoadDistantCellDlodAll` passes `mesh.origin` (shared)
  through to `LoadFromDlod`; (b) **stop sharing the single-dir pointer** — for
  a single-direction `.dlod`, load the mesh into slot 0 only, slots 1..3 null
  (the draw fallback `meshes[d] → meshes[0]` handles null slots). Removes the
  shared-pointer path; `FreeEntries` dedupe (Inc 2) stays as defense-in-depth.
- Error paths: unchanged.

##### src/user/gameplay/render/lvl_room_renderer.{hpp,cpp}
- What changes: add a **block-only draw mode** for pass-shared matrices.
- Function(s):
  - `void DrawBlockOnly() const` (new): run `rspq_block_run(block_)` + the
    precomputed counter sums, **without** touching the matrix stack. Guards
    like `Draw()` (block_ null → legacy per-run emission **without** a matrix
    push — the caller's shared matrix is already on the stack; must not add
    one).
  - `void SetCameraPosition(const Vec3&)` — no-op when
    `uses_external_matrix_` is set (the caller owns the matrix).
- Data shapes: add `bool uses_external_matrix_ = false` + setter
  `void SetExternalMatrixOwner()`. **The flag is set only on distant-loaded
  meshes** — `TileStreamer`'s near `LvlRoomRenderer`/`TexturedRoomRenderer`
  paths must keep their per-frame matrix rebuilds untouched.
- Integration points: `DistantWorldRenderer::Render` (below); near pass
  unchanged.
- Error paths: `block_ == nullptr` → per-run emission without a push.

##### src/user/gameplay/render/distant_world_renderer.{hpp,cpp}
- What changes:
  - `Load()`/`LoadDistantCellDlod*`: pass the **shared origin** from the DLOD
    header (`mesh.origin`) to `LoadFromDlod` instead of the per-cell
    `rs.render_origin` (the header is the source of truth for packing; also
    kills any future bake/manifest origin drift). Store it as `shared_origin_`.
  - New member `Vec3 shared_origin_`; a `T3DMat4FP*` for the pass matrix
    (allocated once, or per-frame from `arena_`).
  - `Render()`: build the shared matrix once per frame
    (`scale 1/kLodScale, translate shared_origin_ - camera_pos_` — host-safe
    helper + test), `t3d_matrix_push(shared)` once, draw all culled cells via
    `mesh->DrawBlockOnly()`, `t3d_matrix_pop(1)` once. **Remove** the per-cell
    push/pop and the per-mesh `SetCameraPosition` loop.
  - `SetCameraPosition(camera_pos)`: store `camera_pos_` only (rebuild the
    shared matrix in `Render`) — no per-mesh loop.
- Function(s): `BuildSharedPassMatrix(camera_pos, shared_origin, lod_scale) ->
  T3DMat4` (host-safe, in `lod_math.hpp` or a new `distant_pass_matrix.hpp`),
  `Render` draw loop.
- Data shapes: `shared_origin_` + matrix added.
- Integration points: `OpenWorldRenderer::SetCenter` (calls `distant_->Load`)
  and `SetCameraPosition` — the latter stops the 180-matrix storm.
- Error paths: `shared_origin_` unset (no cells loaded) → Render no-ops.

##### tests
- New Pattern A: `distant_shared_matrix_contract.cpp` — `BuildSharedPassMatrix`
  math (translation = shared_origin − cam, scale = 1/kLodScale); assert the
  full-map diagonal packs inside int16 at 0.25 and that the per-map ceiling
  (`diagonal × kLodScale ≤ ~28000`) is checked. **Wire into
  `tests/run_host_tests.sh` here.**
- `tests/distant_lod_contract.py`: DLOD header origin == the shared map center
  for all cells; version == 2.
- `tests/dlod_format_contract.cpp`: update the embedded synthetic blob's
  `version` to 2 and add a "v1 blob → ParseDlod returns -1" case (fail-loud).

#### Edge cases
- The DLOD header origin now differs from the manifest `render_origin` — the
  loader uses the **header** origin; assert `pos_scale == kLodScale` still
  holds.
- Stale v1 `.dlod` (per-cell origins): `ParseDlod` returns -1 → cell skipped
  (non-fatal), never misrendered. The Makefile re-bake dependency makes v1
  artifacts the exception.
- Matrix push/pop balance: exactly one push + one pop per distant pass; the
  `block_ == nullptr` fallback must not add a push.
- `uses_external_matrix_` must not leak to near-pass meshes (see Data shapes).

#### Verification
- Run: host tests green (new contract); re-bake; `./compile-rom.sh` clean.
- Device (user): whole-map view — `[counters]` counts unchanged, `[memory]`
  no regression, visual identical; profiler `distant` phase drops (fewer
  matrix rebuilds).
- Done: shared matrix live, one push per pass, v2 fail-loud, int16 contract
  green.

### Inc 4 — Extent-aware distant culling (M)
**Status:** done
**Depends on:** none
**Unblocks:** 5
**Done criteria:** a distant cell is culled only when its **whole XZ AABB** is
outside the camera cone + depth range (with the cone-intersects-interior case
explicitly kept); horizon cells stop popping at the screen edge during a slow
turn; host contract covers edge cases.

#### Files to touch

##### src/user/gameplay/render/lod_math.hpp
- What changes: add an AABB-aware frustum test with a concrete slack formula.
- Function(s):
  ```cpp
  // Cull a distant cell only when its whole XZ AABB is outside the camera
  // cone. Per-corner angular test widened by atan(half_diag / dist) so the
  // cone cannot pass through the cell interior with all 4 corners outside;
  // near/far depth slack scaled by the cell half-extent. Host-safe.
  bool CellAabbInDistantFrustum(const Vec3& cam_pos, const Vec3& cam_target,
                                float hfov_deg, float near_d, float far_d,
                                const AABB& aabb,
                                float margin = kCullMarginDefault, // 1.15
                                float extent_slack = 1.0f);
  ```
  Semantics: `half_diag = 0.5·sqrt(cell_x² + cell_z²)` (≈170u for a 240u cell);
  for each of the 4 XZ corners at distance `d`, the corner passes the angular
  test if `angle(corner, facing) ≤ half_fov·margin + atan(half_diag / d)`;
  keep the cell if **any** corner passes AND the corner is within
  `[near − extent_slack·half_diag, far + extent_slack·half_diag]`; cull only
  when all 4 fail. This is the float analogue of n64brew2025's
  `CULL_TOLERANCE × lod_scale` slack. **`kCullMargin` moves from the
  `distant_world_renderer.cpp` anonymous namespace into `lod_math.hpp`** as a
  host-safe `inline constexpr float kCullMargin = 1.15f;` so the header helper
  and the renderer share one constant (the renderer's local copy is removed).
- Data shapes: `AABB` from `mappack_loader.hpp`.
- Integration points: `BuildDistantRenderListCulled` (below).
- Error paths: degenerate facing / empty depth range → cull (safe).

##### src/user/gameplay/render/distant_world_renderer.{hpp,cpp}
- What changes:
  - `DistantLodEntry` gains `AABB aabb = {{0,0,0},{0,0,0}}`; `Load()` fills it
    from `rs.world_aabb` (`mappack_loader.hpp`).
  - `BuildDistantRenderListCulled(...)` uses `CellAabbInDistantFrustum` instead
    of `CellInDistantFrustum`. Keep `CellInDistantFrustum` for the host
    contract or fold it.
- Function(s): `Load` (aabb fill), `BuildDistantRenderListCulled` (cull call).
- Data shapes: `DistantLodEntry.aabb`.
- Integration points: unchanged callers (`distant_world_renderer.cpp:148`).
- Error paths: zero-extent AABB → treat as the cell center (today's behavior).

##### tests/distant_cull_contract.cpp
- What changes: add cases —
  - cell straddling the cone edge (center outside, AABB inside) **kept**;
  - **all 4 corners outside but the AABB intersects the cone interior →
    kept** (the exact bug class being fixed);
  - cell fully behind a side plane (AABB clear) **culled**;
  - cell within near-slack **kept**; depth-range edges; zero-extent AABB.
- Error paths: none.

#### Edge cases
- Cells at the map edge with AABBs partially behind the camera: the near-slack
  keeps them until truly behind.
- A very large cell near the camera: margin + `atan(half_diag/d)` slack keep it
  from vanishing at the screen edge.
- `kCullMargin` stays a knob; `extent_slack` default 1.0 (tune on device).

#### Verification
- Run: host tests green (new cases).
- Device (user): slow 360° turn in the whole-map view — no horizon pop-in at
  the left/right screen edge.
- Done: AABB-aware cull live, contract green, no screen-edge pop.

### Inc 5 — Ring/distant overlap + near cone-cull + fog softening (L)
**Status:** done
**Depends on:** 4
**Unblocks:** 6
**Done criteria:** no cell is drawn by both passes (distant skips the near-draw
set); the near pass cone-culls its residents (AABB-cone, no mid-cell cut); the
distant near plane is moved to the ring far edge (~508u) so there is no
gap/overlap; fog onset is lowered (~0.28 × sqrt(kDistantMaxDist2) ≈ 370u) so
the ring boundary sits inside the fog ramp (soft transition). **The boundary
*shape* stays the 3×3 ring outline** — documented as a known artifact within
the 9-cell residency budget (textured coverage to 532u would need 25 resident
cells ≈ 500 KB, out of scope).

#### Files to touch

##### src/user/gameplay/render/distant_world_renderer.{hpp,cpp}
- What changes: skip the exact cells the near pass will draw (overlap handoff).
- Function(s):
  - `void SetNearDrawSet(const int ix[], const int iz[], int count)` (new):
    store this frame's near-draw cell indices (small fixed arrays).
  - `BuildDistantRenderListCulled`/`Render`: skip any distant entry whose
    `cell_ix/cell_iz` is in the near-draw set. Host-safe.
- Data shapes: `int near_ix_[kMaxRing]`, `int near_iz_[kMaxRing]`,
  `int near_count_`.
- Integration points: `OpenWorldRenderer` (below).
- Error paths: set not provided → no skip (draw everything as today).

##### src/user/gameplay/render/lod_math.hpp
- What changes: add the shared near-draw predicate (host-safe, so both
  `open_world_renderer.cpp` and `tile_streamer.cpp` call it and the Pattern A
  test covers it).
- Function(s):
  ```cpp
  // A resident cell is drawn by the near pass iff its AABB intersects the
  // camera cone (near FOV/depth) — no grid-index cut, no radial extent beyond
  // residency (the ring IS the coverage; see D4). Host-safe.
  bool CellAabbInNearCone(const Vec3& cam_pos, const Vec3& cam_target,
                          float fov_deg, float near_d, float far_d,
                          const AABB& aabb);
  ```
  Reuses the Inc 4 corner math. Depth 20..800 is effectively a no-op for the
  ring (all ring cells < 800u, the camera's own cell always in range) — the
  cone is the only effective cull and is cheap (4 corners × a dot product).
- Data shapes: `AABB` from `mappack_loader.hpp`.
- Integration points: `TileStreamer::DrawHighPriority` + the distant skip.
- Error paths: null/zero AABB → draw (safe).

##### src/user/gameplay/render/tile_streamer.cpp
- What changes: `DrawHighPriority` iterates the resident ring and draws exactly
  the cells where `CellAabbInNearCone` is true. Rationale (documented): the old
  grid-index gate was removed because it cut geometry at cell boundaries; an
  AABB-cone test cannot cut mid-cell.
- Integration points: the predicate is also used by the distant skip (below).
- Error paths: null AABB → draw (safe).

##### src/user/gameplay/render/open_world_renderer.cpp
- What changes: **ownership is here.** `Render()` computes the near-draw set
  **once** at the top (iterate the resident ring through
  `CellAabbInNearCone` with `cams.near_cam`), passes it to
  `distant_->SetNearDrawSet(...)`, draws the distant pass, then
  `RenderHighPriority` draws exactly that same set. One computation, one source
  of truth — the distant skip and the near draw can never disagree.
- Error paths: none.

##### Tests
- New Pattern A: `distant_overlap_contract.cpp` — with a camera + ring +
  cone, the distant draw set and near draw set are disjoint; ring-corner cells
  are near-only (the overlap band is gone); an AABB partially in cone →
  near-drawn (no grid-edge cut); a non-resident cell just beyond the ring is
  distant-drawn (no hole). **Wire into `tests/run_host_tests.sh` here.**
- `tests/distant_cull_contract.cpp`: no regression.

#### Edge cases
- The near-draw predicate must use the **near** camera (FOV, depth 20..800),
  the distant skip uses the same cell set (identical predicate result).
- Cells just beyond the ring (not resident): always drawn by the distant pass
  (flat). The near-draw set only ever contains resident ring cells, so the
  distant skip never creates a hole — every non-resident cell stays distant.
- Geometry overflowing a cell boundary: AABB-cone keeps the cell drawn (no
  cut), same rationale as the old gate removal.

#### Verification
- Run: host tests green (new contract).
- Device (user): turn in place — no double-drawn seams, no cut at cell
  boundaries during rotation; the flat/tex transition sits inside the fog ramp
  (softened, boundary shape still square — see D4).
- Done: disjoint passes, no mid-cell cut, transition fog-softened.

### Inc 6 — Dynamic distant streaming (M)
**Status:** done
**Depends on:** 1, 3, 5
**Unblocks:** 7
**Done criteria:** the distant tier is streamed by camera cell — resident =
cells within `kDistantStreamRadius` (Chebyshev, **6**) of the camera cell,
evicted outside (all direction meshes freed via the Inc 2 dedupe); the D5
invariant (`radius ≥ ceil(fog_complete/cell + 0.5)`) is contract-asserted;
eviction happens only in `SetCenter` (never between list-build and draw);
boot loads only the initial radius; repeated `SetCenter`/transitions hold
`[memory]` flat.

#### Files to touch

##### src/user/gameplay/render/distant_world_renderer.{hpp,cpp}
- What changes:
  - New `bool StreamToCenter(const MapSpecV2& spec, const V2RoomSpec& center,
    const char* build_dir)`: compute the target resident set (cells within
    `kDistantStreamRadius` Chebyshev of `center`), **unload** entries outside
    it (via the Inc 2 dedupe fix), **load** missing in-radius cells (reuse the
    per-cell load path from `Load()`). `entry_count_` = resident count.
  - New constant `static constexpr int kDistantStreamRadius = 6;` with the
    invariant comment: worst-case load distance `6·240 − 120 = 1320u` >
    fog-complete 1197u ⇒ a cell is fully fogged before it becomes drawable.
  - (No `Load()` wrapper — `StreamToCenter` is the only entry point. The
    initial resident set is built by the first `SetCenter`, which already
    receives the real spawn cell, not `rooms[0]`.)
  - **Eviction ordering contract (explicit):** `StreamToCenter` runs in
    `SetCenter` (Update phase, before the frame's `Render` builds its draw
    list). No eviction may occur between list-build and draw — freeing a mesh
    still referenced by the render list is a use-after-free. The render list is
    rebuilt after every streaming change (asserted by a host test).
- Function(s): `StreamToCenter` (replaces `Load` entirely).
- Data shapes: `entries_` stays a fixed array; residency = active entry count.
- Integration points: `OpenWorldRenderer::SetCenter` replaces
  `distant_->Load(spec, build_dir)` with `distant_->StreamToCenter(spec, center,
  build_dir)`.
- Error paths: load failure for one cell → skip (non-fatal, today); eviction
  only in `SetCenter` (matching `FreeBlock`'s existing RSP-timing caveat).

##### src/user/gameplay/render/open_world_renderer.cpp
- What changes: `SetCenter` calls `StreamToCenter(spec, center, build_dir)`.
- Error paths: none.

##### Tests
- New Pattern C/Python: `distant_streaming_contract` — for a synthetic 45-cell
  grid, cells within radius are resident and outside are evicted; a moved
  center evicts/loads the correct cells; the D5 invariant
  (`radius ≥ ceil(0.9·sqrt(kDistantMaxDist2)/cell + 0.5)`) holds for the real
  constants; the render list is rebuilt after a streaming change (list indices
  valid against the new resident set). **Wire into `tests/run_host_tests.sh`
  here.**
- Existing contracts: no regression.

#### Edge cases
- Radius < the D5 invariant would show pop-in at the radius edge — the contract
  pins radius ≥ `ceil(fog_complete/cell + 0.5)` = 6.
- Camera at a map corner: the resident set shrinks naturally (cells beyond the
  radius are dropped by distance anyway).
- Boot: the first `SetCenter` (with the real spawn cell) builds the initial
  radius. The old `EntryCount() == 0` gate in `OpenWorldRenderer::SetCenter` is
  removed — streaming re-resolves residency on every transition, so the gate
  would wrongly suppress reloads.
- Eviction must never evict the center cell's distant mesh mid-frame (only in
  `SetCenter`).
- A stale v1 `.dlod` during streaming (Inc 3 v2 fail-loud) → cell skipped
  (non-fatal), never a misrender.

#### Verification
- Run: host tests green (new contract); `./compile-rom.sh` clean; re-bake.
- Device (user): walk the map — cells stream in/out with no visible pop-in
  beyond fog; `[memory]` holds flat across repeated
  `SetCenter`/respawn/transitions; boot log shows a small initial load.
- Done: streaming live, memory flat, no pop-in at the radius edge.

### Inc 7 — Close-out: measurement, docs, device tuning (S)
**Status:** done
**Depends on:** 2, 3, 6
**Unblocks:** —
**Done criteria:** `[counters]` / `[memory]` recorded per the device sheet;
`docs/distant_lod.md` + `docs/perf_budget.md` finalized (shared-origin packing,
painter-sorted semantics, stream knobs + D5 invariant, per-map packing ceiling,
baseline-vs-final table); the distant prim-color dedup is **measured** (see
below) and the decision recorded; full host suite green.

#### Files to touch

##### src/user/gameplay/render/distant_world_renderer.cpp (optional experiment)
- What changes: **no committed sort change.** Distant order stays
  distance-primary back-to-front (D6). If device profiling shows prim-color
  churn matters, the documented experiment is a **clean A/B**: (A) today's
  Z-off + distance-primary sort, vs (B) Z-on + `dominant_material` grouping.
  Enabling the Z test **removes the need for the distance sort** (Z resolves
  order), so (B) is a self-consistent alternative, not an incremental tweak —
  land it only if the Z-test cost is less than the state changes it removes.
  Recorded in `docs/perf_budget.md`, not committed here.
- Error paths: none.

##### docs/distant_lod.md
- What changes: document the shared-origin packing + DLOD v2 (Inc 3), the
  same-geometry painter-sorted direction semantics + the (material, dist-along-
  axis) order invariant (Inc 1), the stream radius knob + the D5 worst-case-load
  invariant (Inc 6), and the **per-map packing ceiling**
  (`map_diagonal × kLodScale ≤ ~28000`) with an assertion note.

##### docs/perf_budget.md
- What changes: final baseline-vs-final table; tuning knobs updated
  (`kDistantStreamRadius`, `extent_slack`, `kCullMargin`, and the fog-onset
  ratio knob from Inc 5); the device capture sheet re-stated for the new
  passes; the Ares timing caveat (unchanged); the D6 dedup experiment note.

##### tests/run_host_tests.sh
- What changes: nothing new (all increments wired their own contracts); final
  full-suite run.

#### Edge cases
- Docs must not drift from the constants (knobs referenced by name).
- The D6 experiment, if tried on device, must be reverted or landed with its
  own evidence — never a silent perf/visual regression.

#### Verification
- Run: `./tests/run_host_tests.sh` full suite green; `./compile-rom.sh` clean
  (from scratch after header/struct changes — repo memory: stale-object ODR).
- Device (user): whole-map view — `[counters]` targets
  (`distant_cells` ≤ ~15, `distant_syncs` ≤ ~30-40), `[memory]` distant share
  ≤ ~300 KB and flat across transitions, 30 fps held; 360° turn + full-map
  walk — no popping/holes/edges.
- Done: all targets met, docs finalized, dedup decision recorded.

## Cross-cutting verification

- **Every increment**: `./tests/run_host_tests.sh` green + `./compile-rom.sh`
  clean. After any header struct change, do a from-scratch clean build
  (`.agents/common-mistakes` + repo memory: stale-object ODR → null-vtable
  crash).
- **Re-bake after Inc 1, 3, 6** (bake-side changes). The Makefile dep
  additions make `make` re-bake on tool changes; otherwise run the documented
  `bake_interconnected_map.py` command. After Inc 3's shared-origin + v2
  change, a stale v1 `.dlod` fails to parse (cell skipped) — never a misrender.
- **New test contracts are wired into `tests/run_host_tests.sh` at their own
  increment** (explicit list, no auto-discovery) — "each increment passes CI
  independently" holds throughout.
- **Device measurement sheet** (user, per `docs/perf_budget.md`): record
  `[counters]`, `[distant-cells]`, `[memory]` in the whole-map view and facing
  the ground at each increment. Track `distant_batches`/`distant_syncs` (draw
  cost), `[memory] used=` (residency, flat across transitions after Inc 6).
- **Ares timing is a proxy, not proof**: Ares runs the ROM at ~0.1 fps under
  software paraLLEl-RDP; use Ares for visual smoke + counters, validate
  frame-time conclusions on the user's emulator (per `docs/perf_budget.md`).
- **Controls/axis convention**: none of these increments touch input, camera
  orientation, or the XZ grid convention (`AGENTS.md` load-bearing axis note).
  If directional selection still feels wrong after Inc 1, re-check the
  `_DIRECTION_NORMALS` ↔ `DirectionalIndexFromDelta` mapping against the axis
  convention **before** touching physics.

## Standards / common-mistakes referenced

- `.agents/common-mistakes/og-map-polygon-winding.md` — applies to: Inc 1 — the
  reorder must preserve CCW winding (contract asserts it).
- `.agents/common-mistakes/dfs-path-prefix.md` — applies to: Inc 6 — streamed
  cell loads use the existing `rom:/lvl/<pack>/<chunk>_distant.dlod` convention;
  host `build_dir` localization unchanged.
- `.agents/common-mistakes/missing-player-start-init.md` — applies to: Inc 6 —
  `StreamToCenter` replaces the boot load; player spawn/respawn ordering in
  `SetCenter` must not change.
- `.agents/common-mistakes/camera-respawn-reset.md` — applies to: Inc 5/6 —
  near-coverage handoff + streaming are render-only; respawn camera reset
  untouched.

## Open questions (CONSIDER from review)

- **Textured coverage (square-boundary decision)**: growing textured coverage
  to the fog onset (532u) needs a 5×5 resident ring (25 cells ≈ 500 KB) — over
  the near budget, so the square boundary is softened by fog instead (D4). If
  the user wants a true radial transition, the near ring must grow (a separate
  memory decision, out of scope here). Confirm the fog-softening choice.
- **Fog onset ratio**: lowering to ~0.28 (≈370u) softens the ring boundary but
  also fogs the far part of the near ring; if that looks wrong, tune the ratio
  in `gameplay_scene.cpp` (the single knob, per D7) before touching anything
  else.
- **Stream radius 6 keeps the whole map resident at center** — accepted and
  documented (the architecture, not the memory delta, is the deliverable). If
  a later map is larger, the D5 invariant scales the radius automatically.
- **`DrawBlockOnly` fallback**: when `block_` is null the legacy per-run
  emission must run without a matrix push — confirm the fallback never
  double-pushes (the shared matrix is on the stack).
- **DLOD v2 shared-verts**: deferred — if on-disk bytes ever matter (bigger
  maps), add a shared-verts section instead of 4× duplicated verts.
- **D6 dedup experiment**: a clean A/B — (A) Z-off + distance sort vs (B)
  Z-on + material grouping. Enabling Z invalidates the distance-sort rationale
  (Z resolves order), so (B) is self-consistent; land it only with device
  evidence that the Z-test cost is less than the state changes it removes.

## Out of scope

- Material-block extraction (`material_apply` blocks) — future work; Inc 7
  records the measurement, not the implementation.
- Raising `kDistantMaxDist2` to reveal more of the map — a visual decision,
  deliberately deferred (Inc 6's streaming makes it viable later).
- Two-tier distant (mid/far) — rejected earlier, stays rejected.
- DLOD v2 shared-verts / packed materials — deferred.
- Near-pass packing format (float32 LVL2) — unchanged.
