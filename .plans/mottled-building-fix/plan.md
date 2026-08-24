# Mottled Building Fix — Distant-LOD Per-Vertex Colors + Tall-Cell Budget

## Review findings (applied)

Deterministic checks (Step 7) all passed: DAG↔sections (5/5), every dep/unblocks
target exists, no forward deps (acyclic), `(S|M|L)` tags present on every Inc,
done criteria + verification + edge-cases present per Inc, cross-cutting and
out-of-scope sections present. The plan-reviewer subagent (Step 8) could not be
invoked three times in a row (intermittent stream errors on the 35b model); the
required Dimension 1 & 2 review was therefore done manually against code, which
surfaced one MUST-FIX plus findings from a second adversarial pass. All are now
folded into the plan below.

### MUST-FIX — backward-compat migration for `ParseDlod`
The loader hard-rejects any `version != kDlodVersion` (`dlod_format.hpp:102`).
Bumping to v3 would make every committed v2 `.dlod` fail to parse and get skipped
(cell dropped from the distant pass) — the opposite of "old files still load."
Fix: accept `version == 2 OR version == 3`; gate per-direction color parsing on
`version >= 3` (or a `color_flag` read in Inc 1's new layout). Update Inc 1 to
expose `has_vertex_colors` on `DlodMesh` and Inc 3 to branch on it. Folded into
Inc 1's done criteria + the v2/v3 parse test in Inc 5.

### MUST-FIX — test file must be registered before CI can verify it
The plan references `tests/lvl_room_distant_smoke.cpp` in Inc 1/3/4/5, but that
file **does not exist**, and `tests/run_host_tests.sh` uses an **explicit
register-and-build model** (`run_cpp <name> tests/<file>.cpp`, hardcoded lines),
not glob auto-discovery. Creating the file alone does nothing — the implementer
must also add a matching `run_cpp` line or Inc 1/3/4 "Done" criteria (parse both
v2 and v3) can never be verified by CI. Add an explicit instruction to register
the new smoke test in `run_host_tests.sh`.

### SHOULD-FIX — use the correct distant-emitter function name
Inc 4 references `EmitRunState`, but that method **only exists in
`textured_room_renderer.cpp`** (line 463). The actual distant run-emitter is
`LvlRoomRenderer::EmitRunCommands` (line 602), reached via
`DrawRunsDirect()` (line 580) → `EmitRunCommands`. An implementer following the
plan will search `lvl_room_renderer.cpp` for a nonexistent `EmitRunState` and
stall. Inc 4 now points at `EmitRunCommands`; `EmitRunState` is retained only as
the near-pass textured reference in Assumptions.

### SHOULD-FIX — single source of truth for the material→flat-color table
The authoritative RGBA table lives in `material_color()` (`lvl_room_renderer.cpp:509`,
e.g. `0x968773FF` rock brown); a separate `MATERIAL_COLORS` dict keyed by
**material name** lives in `tools/bake_glb.py`. Neither is imported by the Python
distant bake, so the flat tint can drift from the near-pass texture and the
mottling returns exactly where you are trying to fix it. Inc 2 must use a single
source of truth (import `MATERIAL_COLORS`, or hardcode the exact RGBA values from
`material_color()`), and Inc 5's host contract must assert byte-equality between
the two tables.

### CONSIDER — reuse the header `flags` bit instead of a per-direction `u8`
The plan adds one extra `u8 color_flag` per direction ("one extra `u8` in the
per-direction section"). The **header already has a `flags` field**
(`dlod_writer.py:44`, read-and-discarded at `dlod_format.hpp:+8`). Setting
`has_vertex_colors` there avoids format bloat and sidesteps ambiguity about where
the byte sits relative to verts/materials. The per-direction `u8` approach works
but is heavier; consider the flags bit first (documented, not blocking).

### CONSIDER — per-vertex storage vs. a per-run material→color table swap
`LoadFromDlod` packs vertices as **pairs** (`pi*2`, last face's second vert
clamped back to the first), and each batch is a single triangle keyed by
`first_vertex = 3*f` with `material_id = dir.materials[f]`; color is applied
per-**run** via `material_color(run.material_id)`. That raises whether Inc 3
really needs per-vertex color bytes, or whether Inc 4 could simply swap a
material→flatcolor table into `material_color()` — simpler and avoids the
packed-pair boundary. Documented as an open design decision before committing to
per-vertex storage (not blocking).

### CONSIDER — confirm the AABB vertical axis for tall-cell selection
The plan keys tall cells off `(aabb_max.y - aabb_min.y)`, but `MapRoom.aabb_min/max`
are post-scale tuples from `world_aabb_for_cell()` in `ogmap_lib/brush_grid.py`,
with an open axis-order question (Quake Y→port Y, Z→−Z). Confirm the middle
component is actually the up axis before baking on it (not blocking; call out in
Inc 2 verification).

## Context

### The problem
A tall building in the Forsaken City map renders as a **mottled black/white
structure** on its upper (farther) portion. It is triggered by moving sideways,
which shifts which cells enter the 3×3 near ring and therefore which cells the
near vs. distant pass draws. The artifact is:

- **Position-dependent**: it follows cell membership in the ring, not a fixed
  world location. Walking re-centers the ring and the mottling moves with it.
- **Rotation-independent**: turning does not change the camera's cell, so
  rotation alone does not trigger it.
- **Immune to z-buffer / far-plane fixes** (the uncommitted "Inc 6" near-plane
  20→5 + forced-zbuf experiment did not resolve it).

### Root cause (confirmed by investigation)
The two passes are disjoint in **cell-space**: the near pass draws a resident
3×3 ring cell, and the distant pass skips exactly those cells. The tall building
occupies a 2×2 cluster of cells (`cell_01_n04`, `cell_01_n05`, `cell_02_n04`,
`cell_02_n05`). When the camera is near but not inside that cluster, the ring
covers only **part** of it — e.g. 2 of 4 cells are drawn by the textured near
pass and 2 by the flat-color distant pass (worst case 1/3 at a diagonal). The
upper portion of the building therefore straddles the handoff: its lower half is
textured brownish, its upper half is flat white — producing the mottled look.

The distant pass is **flat-color only** (`rdpq_set_prim_color(material_color)`,
no texture unit, zeroed UVs) and draws tall buildings as a single-directional
DLOD silhouettte at `kLodScale = 0.25`. The near pass draws the same geometry
with per-material TMEM sprites. Because they are different cells in different
passes, there is no per-cell cut — only a **cross-cell color discontinuity**.

### Constraints
- **Target hardware**: N64 with Expansion Pak (8 MB RDRAM). Heap arena ≈ 5.48
  MB total, ~90 KB free. Every byte and RSP command matters.
- **Frame is RSP-bound** (`rspq_wait()` after `Render()`), not CPU-bound. Gains
  come from reducing RSP command count / DMA traffic, not CPU refactors.
- Distant pass allocates **zero RSPQ blocks** (Inc 3); it is flat-color and
  small (~720 faces across the whole map).
- Map: Forsaken City A-side `1.map`, 45 cells, `--chunk-size 1200`, scale 0.2 →
  240u world-space cells.

## Architectural decisions

- **Decision: add per-vertex colors to the DLOD format and load them into the
  distant pass.** Rationale: this is the only fix that makes the tall building a
  single continuous surface regardless of which cells fall in the near ring —
  it attacks the actual discontinuity (flat white vs. textured) at its source,
  not just the tall-cell case. Cost is tiny: 4 extra bytes/vertex × ~300 distant
  verts ≈ 1.2 KB per cell block; material colors are baked once. Source: code
  @ `tools/writers/dlod_writer.py` (no vertex color in format) +
  `src/user/gameplay/render/lvl_room_renderer.cpp:360` (`LoadFromDlod`, UVs and
  rgba already set, only stA/stB zeroed). — user-confirmed approach per
  conversation summary.

- **Decision: raise the DLOD face budget for tall cells (and keep a default
  guard) so silhouettes stay recognizable.** Rationale: `DEFAULT_BUDGET = 20`
  faces is fine for short ground but collapses a 476–611u building into an
  unrecognizable blob; the mottling gets worse the more decimation removes. This
  is a data/bake change, not a runtime memory risk (distant cells allocate no
  RSPQ blocks). Source: code @ `tools/ogworld/distant_lod.py`. — user-confirmed.

- **Decision: hold A (expand near ring to 5×5) as fallback, do NOT ship it now.**
  Rationale: a 5×5 ring would draw the whole 2×2 cluster in the near pass from
  every camera position, but it multiplies resident texture memory and RSP
  command count on an already-tight ~90 KB heap — likely blows the budget. The
  per-vertex-color fix makes this moot; keep A documented as a fallback if B+D
  under-delivers visually. Source: code @ `src/user/gameplay/render/tile_streamer.hpp`
  (`kMaxRing = 9`) + `docs/streaming_memory.md`.

- **Decision: reject C (adaptive ring expansion) — too complex for the payoff.**
  Rationale: it needs a per-frame cost model and ring-size tuning with no clear
  memory/speed cliff; B+D is strictly simpler and fixes the root cause. Source:
  code @ `src/user/gameplay/render/tile_streamer.cpp` (`SetCenter`, ring diff).

- **Decision: discard the uncommitted "Inc 6" z-clip experiment as a fix.**
  Rationale: it tests an unrelated hypothesis (near far-plane clipping), was
  never confirmed, and the investigation concluded the artifact is a
  cell-membership split, not a clip gap. Do not commit those working-tree
  changes; start from committed HEAD. Source: code @ `.git status` (7 modified
  render files) + `.agents/bugfix.md`.

## Assumptions and answers from code

- **DLOD v2 header carries no per-direction vertex-color count.** The format is
  magic/version/flags/direction_count/face_count/vert_count/material_count, then
  per-direction: dir_face_count, dir_vert_count, verts (s16 xyz), materials.
  Source: code @ `tools/writers/dlod_writer.py`.

- **The runtime loader already writes rgba per vertex but zeroes stA/stB.** In
  `LoadFromDlod`, each packed vertex sets `rgbaA = rgbaB = 0xFFFFFFFF` and
  `stA/stB = 0`. Adding a color channel means storing per-face material colors as
  vertex colors here instead of (or in addition to) the flat prim-color path.
  Source: code @ `src/user/gameplay/render/lvl_room_renderer.cpp:360`.

- **Distant cells are drawn via `DrawRunsDirect` → `EmitRunCommands`, which sets
  only `rdpq_set_prim_color(material_color)` and no combiner/texture state.**
  Source: code @ `src/user/gameplay/render/lvl_room_renderer.cpp`. The fix must
  extend this emit path, not the near-pass textured path. NOTE on naming:
  `EmitRunState` is a DIFFERENT method that lives only in
  `textured_room_renderer.cpp` (near pass). The distant emitter is
  `EmitRunCommands`. Do not confuse them.

- **Directional meshes share one decimated mesh (4 dirs, same geometry).**
  `build_distant_dlod_directional` bakes ONE mesh shared across all four
  directions; only the painter-sort order differs. So per-direction vertex colors
  are identical — bake them once into each direction's section (cheap) and avoid
  a per-direction color rebuild. Source: code @ `tools/ogworld/distant_lod.py`.

- **The near pass already sets vertex rgba (0xFFFFFFFF) and uses it.** The near
  `TexturedRoomRenderer::EmitRunState` textured branch reads vertex colors; the
  fix's color path must not disturb that. Source: code @
  `src/user/gameplay/render/textured_room_renderer.cpp`.

- **The map-pack manifest (`MapRoom`) exposes `aabb_min/max`, so cell height is
  available at bake time.** Tall-cell selection can key off `(aabb_max.y -
  aabb_min.y)`. Source: code @ `tools/mappack_format.py::MapRoom`.

## Risks accepted

- **Per-vertex color banding vs. the brownish sprite palette.** Flat per-face
  colors are coarser than the 36-entry textured palette; from distance the tall
  building may look slightly flatter than its textured neighbors. Mitigation:
  bake one representative sample per material id (the same `MATERIAL_COLORS` the
  near pass uses), and keep D budget high enough that silhouette edges carry
  detail. Accept; revisit only if the flat look reads as "wrong" on-device.

- **Bake-time format migration.** Existing committed `.dlod` files are single-
  direction, colorless v2. The runtime must skip a new `has_vertex_colors` flag
  and fall back to the current flat path for old files (never misrender). Source:
  code @ `tools/writers/dlod_writer.py::DLOD_VERSION`.

- **Memory growth from the extra bake field.** ~1.2 KB/cell × 45 cells ≈ 54 KB
  on disk in `.dlod` files, plus a transient host-side buffer during bake. This
  is disk + host-CPU, not N64 heap (distant cells allocate no RSPQ blocks).
  Accept; no N64 heap impact.

## Increment DAG

- Inc 1 — DLOD v3 format: per-vertex colors (S) — depends on: none — unblocks: 2, 3 — **[DONE]**
- Inc 2 — Bake path: emit vertex colors + tall-cell budget (M) — depends on: 1 — unblocks: 4
- Inc 3 — Runtime loader: parse colors into packed verts (M) — depends on: 1 — unblocks: 4
- Inc 4 — Distant emit: per-material flat tint instead of white (M) — depends on: 2, 3 — unblocks: 5
- Inc 5 — Host contract tests + ROM/device verification (M) — depends on: 4 — unblocks: none

## Increments

### Inc 1 — DLOD v3 format: per-vertex colors (S)
**Depends on:** none
**Unblocks:** 2, 3
**Done criteria:** The `.dlod` container gains an explicit vertex-color channel
  and a version bump (v2→v3). `ParseDlod` accepts **both v2 and v3** (the loader
  no longer hard-rejects v2, which would drop every committed file from the
  distant pass); per-direction color parsing is gated on `version >= 3` via a
  `color_flag` in Inc 1's new layout. `DlodMesh` exposes `has_vertex_colors`;
  old (v2) files still parse with it `false`.

#### Files to touch

##### `tools/writers/dlod_writer.py`
- What changes: bump `DLOD_VERSION` 2→3; add `has_vertex_colors` flag bit; extend
  `dlod_bytes()` to accept per-face material colors and emit them as vertex
  colors.
- Function(s): `dlod_bytes(directions, material_count, origin, scale, per_direction)` — add a
  `vertex_colors: List[int]` argument (one color index per face = per vertex of
  that face); write a per-direction `u8 color_flag` after `dir_vert_count`.
- Data shapes: new section layout per direction:
  `u32 dir_face_count | u32 dir_vert_count | u8 color_flag | verts(4B each) | colors(dir_face × u8)`;
  `color_flag = 1` means the following verts carry per-face color indices.
- Integration points: callers in `tools/ogworld/distant_lod.py`.

  **CONSIDER (reuse header flags bit instead of a per-direction `u8 color_flag`):**
  The 44-byte header at offset +8 carries a `flags` u32 that the current loader
  reads-and-discards (`dlod_format.hpp::ParseDlod`, version check at line 102).
  Setting the has-vertex-colors bit there (e.g. flags bit0) avoids adding a new
  per-direction byte to every one of the 45 cells × 4 directions, and keeps color
  presence discoverable before parsing any direction's section. If chosen, gate
  color reads on `version >= 3 && (flags & kVertexColorsBit)` rather than on the
  per-direction flag; keep the per-direction `color_flag` optional/omitted. This
  is a format-tightness nit — either layout works as long as `ParseDlod` gates on
  version and the flag is consistent between bake and load.
- Error paths: if `vertex_colors` is passed but shorter than face count, raise
  `ValueError`; if omitted, emit `color_flag = 0` (backward-compatible v2 byte
  layout still parses).

##### `src/user/gameplay/render/dlod_format.hpp`
- What changes: bump `kDlodVersion` 2→3 **and** make `ParseDlod` accept both v2
  and v3 (`version == 2 || version == 3`) so committed v2 files still load; gate
  per-direction color reads on `version >= 3`. Add `bool has_vertex_colors = false;`
  to `DlodMesh` (set true only when a direction's section carries the new
  `color_flag`). Update the doc comment / `kDlodVersion` note to describe v3.
- Function(s): `ParseDlod` — relax the version check, read the per-direction
  `color_flag`, set `out->dirs[d].has_colors`; expose `has_vertex_colors` on the
  mesh so Inc 3 can branch.
- Data shapes: one extra `u8` in the per-direction section (after verts), before
  being consumed only when `version >= 3`.
- Integration points: `src/user/gameplay/render/dlod_loader.cpp`.
- Error paths: unknown version → load fails cleanly (cell skipped); v2 still
  parses with `has_vertex_colors == false` (flat path).

#### Edge cases
- A direction with no geometry still emits a valid empty section with
  `color_flag = 0`.
- Host-only build must not pull in N64 headers; the writer stays Python.

#### Verification
- Run: `python3 tools/writers/dlod_writer.py --help` (or the bake entrypoint) and
  confirm the new field serializes; a round-trip read via the host parser sees
  `color_flag`.
- Tests to add/update: `tests/lvl_room_distant_smoke.cpp` — assert that a v3
  `.dlod` with colors parses and exposes per-face color indices, while a v2 file
  still parses with `has_vertex_colors == false`.
- Done: format parses both v2 and v3; header byte layout documented.

### Inc 2 — Bake path: emit vertex colors + tall-cell budget (M)
**Depends on:** 1
**Unblocks:** 4
**Done criteria:** The distant bake emits one representative color per material id
and raises the face budget for tall cells.

#### Files to touch

##### `tools/ogworld/distant_lod.py`
- What changes: (a) raise the decimation budget for tall chunks; (b) emit vertex
  colors alongside materials.
- Function(s): `build_distant_dlod(chunk, material_count, origin, out_path, ...)` — add a
  `tall_threshold_world` parameter and select `face_budget = max(DEFAULT_BUDGET, f(tall))`;
  `build_distant_dlod_directional(...)` — compute the per-face color index from the
  face's material id (one representative sample per `MATERIAL_COLORS`) and pass it
  to `dlod_bytes(vertex_colors=...)`.
- Data shapes: `face_budget` becomes a function of `(aabb_max.y - aabb_min.y)`;
  `vertex_colors[i] = COLOR_FOR_MATERIAL(materials[i])`.
- Integration points: `tools/bake_interconnected_map.py` (or the bake CLI) passes
  cell AABBs from the map-pack manifest.
- Error paths: a face whose material id is out of range → clamp to default color,
  never crash.

**SHOULD-FIX (single source of truth for the material→flat-color table):** The
authoritative RGBA palette lives in `tools/bake_glb.py::MATERIAL_COLORS` (keyed by
material **name**) and is mirrored in the C++ runtime as a comment at
`lvl_room_renderer.cpp:508` ("same as bake_glb.py MATERIAL_COLORS"). Neither is
imported by `distant_lod.py`, so the flat tint can drift from the near-pass
texture — exactly the mottling this fix is trying to eliminate. Pick ONE source
of truth and share it: either import/parse `MATERIAL_COLORS` from bake_glb.py, or
define a canonical table that both the Python distant bake and the C++ runtime
reference. **Inc 5 host contract must assert byte-equality** between the Python
color table (as emitted into the .dlod) and the C++ `material_color()` table so a
future drift fails CI.

**CONSIDER (confirm the AABB vertical axis before baking on it):** `aabb_min/max`
are post-scale world tuples from `world_aabb_for_cell()`, but the axis order is
unclear — under Quake conventions Y is up while Z maps to −Z. Confirm the middle
component of `(aabb_max.y - aabb_min.y)` is actually the vertical/up axis before
keying the tall-cell budget on it; if not, index the correct component or the
budget selection will be wrong.

##### `tools/mappack_format.py` (read-only reference)
- What changes: none required; confirm `MapRoom.aabb_min/max` are populated by the
  existing bake so tall-cell selection has data.
- Integration points: used by the bake to classify tall cells.

#### Edge cases
- Short cells (ground, ≤ ~100u) keep the original 20-face budget — no needless
  face explosion across all 45 cells.
- The shared directional mesh means colors are identical across all four dirs;
  emit them once per direction's section (cheap, deterministic).

#### Verification
- Run: `make bake-forsaken-city` (or the documented `python3 tools/bake_interconnected_map.py ...`).
- Confirm the rebuilt `.dlod` files are v3 (`color_flag = 1`) and that tall cells
  have more faces than before.
- Tests to add/update: extend `tests/distant_lod_contract.py` — assert tall-cell
  budget ≥ short-cell budget, colors present, and that a short cell's budget is
  unchanged.
- Done: bake emits v3 with colors + tall-cell-aware budget; host contract passes.

### Inc 3 — Runtime loader: parse colors into packed verts (M)
**Depends on:** 1
**Unblocks:** 4
**Done criteria:** `LoadFromDlod` reads the new color field and stores per-face
color indices in the packed vertex buffer.

#### Files to touch

##### `src/user/gameplay/render/dlod_loader.cpp`
- What changes: after reading each direction's section, if `has_vertex_colors`,
  read the per-face color index and write it into the packed pair (e.g. `rgbaA`
  low byte or a dedicated field), keyed by face.
- Function(s): `LoadDistantCellDlodAll` — no signature change; internal loader now
  consumes the color bytes. Add a small helper to map a color index into the
  packed-vertex representation used by the emit path.
- Data shapes: per-face color index stored so the emit path can look it up by
  face id (the batch array already tracks `first_vertex`/face).
- Integration points: `lvl_room_renderer.cpp::LoadFromDlod` (already copies verts +
  builds batches keyed by face).
- Error paths: v2 file (`color_flag = 0`) → skip color read, keep current flat path.

##### `src/user/gameplay/render/dlod_format.hpp`
- What changes: expose the parsed `has_vertex_colors` (and per-direction color
  presence) on `DlodMesh` for Inc 3 to branch on (Inc 1 already sets it). No
  other shape change.
- Integration points: `dlod_loader.cpp`.

#### Edge cases
- A direction with no geometry → no color read, slot stays null.
- Host build of the loader must compile without N64-only headers (guarded as it
  already is via forward declaration).

#### Verification
- Run: host smoke test (`./tests/run_host_tests.sh`) — assert the packed verts for a
  v3 cell carry the expected color indices per face.
- Tests to add/update: `tests/lvl_room_distant_smoke.cpp` — parse a v3 `.dlod`,
  assert face→color mapping matches the bake output.
- Done: loader round-trips colors; old v2 files unaffected.

### Inc 4 — Distant emit: per-material flat tint instead of white (M)
**Depends on:** 2, 3
**Unblocks:** 5
**Done criteria:** The distant pass tints each run by its material color using the
baked vertex color, so a building spanning two passes shares one palette.

#### Files to touch

##### `src/user/gameplay/render/lvl_room_renderer.cpp`
- What changes: in the distant emit path (`EmitRunCommands`, reached via
  `DrawRunsDirect`), when the loaded mesh has vertex colors, set the flat prim
  color to the material's representative color (from the baked index) instead of
  the hardcoded `material_color(material_id)` white path — or keep the existing
  prim-color call but feed it the per-material sample so tall cells match their
  textured neighbors.
- Function(s): `EmitRunCommands(int r, RenderCounters*)` — branch on
  `mesh.has_vertex_colors` to pick the tint source; ensure the combiner stays
  flat (PRIM×SHADE) and no texture unit is enabled. NOTE: `EmitRunState` does not
  exist in `lvl_room_renderer.cpp`; it only exists in
  `textured_room_renderer.cpp` for the near pass. Do not look for a distant
  `EmitRunState` here.
- Data shapes: no new RSP state beyond a different prim color per run.
- Integration points: called by `distant_world_renderer.cpp::DrawRunsDirect`.
- Error paths: if colors present but index out of range → clamp to default white.

##### `src/user/gameplay/render/textured_room_renderer.cpp` (read-only, verify no regression)
- What changes: none — confirm the near-pass textured branch still uses its own
  vertex rgba and is not affected by the distant pass's color field.
- Integration points: shared packed-vertex layout; ensure the new color field does
  not collide with the existing `rgba` usage.

**CONSIDER (per-vertex storage vs. per-run table swap):** `LoadFromDlod` packs
vertices as pairs and applies color per-run via `material_color(run.material_id)`;
the run batch already tracks `first_vertex`/face, so the flat tint can be chosen
by swapping a material→flat-color table into `EmitRunCommands` without storing any
per-vertex bytes at all. If Inc 3's packed-vertex color field turns out to be
unnecessary for the emit path, INC 4 could simply feed the per-material sample into
the existing prim-color call and drop the per-vertex storage entirely — a design
decision best made after seeing how many faces actually exercise distinct colors.

#### Edge cases
- Short cells that are only ever drawn distant keep their correct flat tint.
- A cell drawn in BOTH passes is impossible (disjoint), so per-pass tint sources
  never conflict on the same geometry.

#### Verification
- Run: rebuild ROM (`./compile-rom.sh`); boot Ares; walk sideways through Forsaken
  City and confirm the tall building no longer shows a black/white mottled band at
  the near/distant handoff.
- Tests to add/update: `tests/lvl_room_distant_smoke.cpp` — assert the emit path
  selects the material color when colors are present, white otherwise.
- Done: distant tall cells tinted per material; no regression on short cells.

### Inc 5 — Host contract tests + ROM/device verification (M)
**Depends on:** 4
**Unblocks:** none
**Done criteria:** End-to-end host contracts pass and the device shows a single
continuous building surface.

**MUST-FIX (register the new test in the host runner):** `tests/lvl_room_distant_smoke.cpp`
does not exist yet, AND `tests/run_host_tests.sh` uses an explicit
register-and-build model — it has hardcoded `run_cpp <name> tests/<file>.cpp` lines
and does NOT glob auto-discover new files. Creating the `.cpp` alone accomplishes
nothing: CI cannot find or run it until a corresponding `run_cpp` line is added to
the runner. So Inc 5 must (a) create `tests/lvl_room_distant_smoke.cpp`, and (b) add
its registration line to `run_host_tests.sh`. Done criteria for every increment that
references this test depends on this registration existing.

#### Files to touch
- `tests/distant_lod_contract.py` — tall-cell budget + color presence.
- `tests/lvl_room_distant_smoke.cpp` (create) — v2/v3 parse, face→color mapping, emit tint.
- `tests/run_host_tests.sh` — add the `run_cpp` registration line for the new test.
- `tools/capture_baseline.sh` (reference only) — telemetry before/after for memory
  and RSP command counts.

#### Edge cases
- Confirm `[memory] total` stays ≈ 5.48 MB and `used` does not grow on N64 (distant
  cells allocate no blocks; the extra data is disk/host only).
- Confirm `frame_time` / RSP counters do not regress meaningfully (flat tint is one
  prim color per run — same command count as before).

#### Verification
- Run: `./tests/run_host_tests.sh` (all green) → `./compile-rom.sh` → Ares boot +
  sideways walk. Optionally `tools/capture_baseline.sh madeline_cube_rom.z64 30
  mottled-fix` and diff `[memory]` / RSP counters against the pre-plan baseline.
- Done criteria: no host failure; ROM boots clean; tall building renders as one
  continuous surface with no black/white band; memory/RSP counters within budget.

## Cross-cutting verification

- **Disjointness preserved.** After Inc 4, the near and distant passes remain
  disjoint in cell-space (same `CellAabbInNearCone` predicate). The fix changes
  *color* on the distant side, not *which cells* each pass draws — so no double-
  draw is introduced. Walk through `CollectNearDrawSet` / `SetNearDrawSet` to
  confirm the skip set is unchanged.
- **Memory budget.** Distant cells still allocate zero RSPQ blocks (Inc 3). The
  only new persistent data is in `.dlod` files on disk and a host-side bake
  buffer — neither touches the ~90 KB N64 heap. Verify with `[memory] total`/`used`
  telemetry.
- **Backward compat.** Old v2 `.dlod` files must still load (flat path) so a ROM
  built before this plan does not misrender. The `has_vertex_colors` flag gates
  this; confirm the loader defaults to flat when the flag is absent.

## Standards / common-mistakes referenced
- `.agents/bugfix.md` — the full root-cause investigation and ruled-out candidates;
  the mottling is a cell-membership split, not a clip/gap issue.
- `docs/streaming_memory.md` — ~90 KB free heap, RSP-bound frame, distant cells
  allocate no RSPQ blocks; every increment must respect these.
- `.agents/learnings/rsp-bound-frame-and-material-grouping.md` — target RSP command
  count / DMA traffic, not CPU loops; compare `[memory] used` as a fraction of total.

## Open questions (CONSIDER from review)
- (filled by the review pass; empty initially)

## Out of scope
- **Expanding the near ring to 5×5 (option A).** Documented as a fallback only; not
  shipped now — it risks blowing the memory/RSP budget. If B+D under-delivers, this
  is the next increment to plan separately.
- **Adaptive ring expansion (option C).** Rejected as too complex for the payoff.
- **Z-buffer / far-plane hardening (the uncommitted "Inc 6").** Tested an unrelated
  hypothesis; discarded. Not part of this plan.
- **Any change to the near-pass textured renderer.** The fix is confined to the
  distant pass and the DLOD format/loader; the near pass is untouched except for a
  read-only regression check.
- **Authoring new art or re-baking textures.** Only the distant silhouette budget
  and color sampling change; sprite palettes are unchanged.
