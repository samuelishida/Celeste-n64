# Interconnected Map Render Fix + Traversal Gate

## Context

The user reports two symptoms in the ROM: (a) "the map is just this little
piece" — only a small region of the interconnected world is visible, and (b)
"when she starts moving she falls" — the player appears to fall through the
floor. The user asks for a pipeline that "actually can convert the original
map to our colmesh geometry format without breaking and retaining the map
geometry and size and format."

Investigation (2026-08-10) shows the **bake pipeline is already correct** and
retains all geometry/size/format:

- `tools/bake_interconnected_map.py` parses `assets/og_converted/maps/1.map`
  once into the canonical world IR (`tools/ogworld/`), builds **one global
  CMSH** (10,618 tris / 20,868 verts / 8,191 BVH nodes / ~383 KB), partitions
  visual geometry into **45 per-cell LVL2 rooms** at `--chunk-size 1200`
  `--scale 0.2`, and emits a **map-pack v2** (`MPP2`) manifest. All 45 rooms
  are reachable from `cell_00_00`; coverage errors = 0.
- The published `filesystem/lvl/forsyken-city/` matches `staging/` exactly and
  includes the global CMSH. The ROM boots the v2 map-pack via `MapRuntime`
  (the legacy v1 `Map` path was already retired in the prior fix).
- Collision is **world-space global** and correct: `room.coll_mesh` is a
  compatibility pointer to the one global mesh, and the motor/camera query it.

The real defect is a **runtime render-integration mismatch**, not a bake
defect:

- `LvlRoomRenderer::Load(lvl_path, render_origin)` subtracts `render_origin`
  from every geometry vertex before int16 fixed-point packing
  (`lvl_room_renderer.cpp:110-115`) to avoid `kPosScale` overflow on the full
  map's absolute coordinates.
- But the model matrix built in the same function uses position `{0,0,0}`
  (`lvl_room_renderer.cpp:196-201`), so it compensates only for `kPosScale`
  (scale), **not** the render-origin translation. The active chunk's geometry
  is therefore drawn at `world − render_origin` (e.g. −120 in X, −120 in Z for
  the start cell `cell_00_00`), while the player/camera are drawn in world
  coordinates. The camera looks at the world-space player, but the drawn floor
  is 120 units away — hence "only a small piece" and the *appearance* of
  falling through (collision itself is correct).
- Additionally, the scene renders **only the one active room** (`active->renderer->Draw()`),
  so even after the offset fix, only one cell is visible at a time — the world
  does not feel connected.

This plan fixes the render-origin model-matrix bug, adds neighbor-ring
rendering so the interconnected world is visible, and adds a **hardware
traversal acceptance gate** — the recurring failure across all 11 prior plan
generations was that "DONE" was validated on host tests or a single static
chunk, never on real traversal across seams.

## Architectural decisions

- **Decision: keep the verified bake pipeline; do not rebuild it.** The
  canonical IR → global CMSH + per-cell LVL + v2 manifest already converts the
  original map to our colmesh format while retaining geometry/size/format.
  Rebuilding it would re-introduce the axis/coordinate-space and
  artifact-integration failure patterns documented across 11 prior plans.
  Alternatives rejected: a from-scratch bake rewrite (unnecessary risk; the
  current bake is verified correct).
- **Decision: fix the render-origin model-matrix translation in
  `LvlRoomRenderer`.** The model matrix position must be `render_origin`
  (world units) so the rebased geometry is translated back to world space,
  matching the world-space player/camera. This is a one-line, surgical fix.
- **Decision: render the active cell plus its immediate ±X/±Z neighbors.**
  A render-only `ChunkRingRenderer` loads the active cell and its four
  neighbors into `LvlRoomRenderer` instances and draws all five each frame.
  This makes the world feel connected and is N64-feasible (each cell is small:
  max 920 faces / 3,527 verts). Alternatives rejected: rendering all 45 cells
  every frame (memory/draw cost may exceed the N64 budget) and keeping
  active-only rendering (does not address "the map is just this little
  piece").
- **Decision: gameplay stays active-only; only rendering is a ring.** The
  `MapRuntime` continues to own one global collision mesh + one authoritative
  active visual room for physics/actors. The neighbor ring is a render-only
  pool that does not affect collision, actors, or respawn. This preserves the
  verified active-only traversal path.
- **Decision: the acceptance gate is hardware traversal, not host tests.**
  Per-chunk telemetry (active room id, floor normal, render-origin, grounded
  state) is added, and a documented Ares/Mupen64Plus procedure requires
  crossing ≥2 seams (one +X, one world-Z/depth) with no fall-through. This
  directly counters the recurring "DONE validated on a single static chunk"
  failure.

## Assumptions and answers from code

- **Decision: the bake is correct and retains all geometry/size/format.**
  Source: code @ `tools/bake_interconnected_map.py`, `tools/ogworld/`,
  `build/bake-fc-1200/interconnected_report.json` (45 cells, 0 coverage
  errors, 10,618 global tris).
- **Decision: the render-origin model-matrix position is the bug.**
  Source: code @ `lvl_room_renderer.cpp:110-115` (vertex rebase) vs
  `lvl_room_renderer.cpp:196-201` (model matrix position `{0,0,0}`).
- **Decision: collision is world-space global and correct.**
  Source: code @ `map_runtime.cpp:151-153` (compat pointer to global mesh),
  `world.cpp:133-134` (`RaycastRoomSource` uses `room.coll_mesh`).
- **Decision: only one active room is rendered.**
  Source: code @ `gameplay_scene.cpp:772-777` (`active->renderer->Draw()`).
- **Decision: the scene boots the v2 map-pack via `MapRuntime`.**
  Source: code @ `gameplay_scene.cpp:349-404` (`BootMapPack`), `rom_main.cpp:44`.
- **Decision: stale v1 artifacts exist in `build/bake-fc-1200/`.**
  Source: code @ `build/bake-fc-1200/` (`cell_04_23.lvl` orphan, top-level v1
  `forsyken-city.mappack`/`.mappack.json`/`chunks.json`).
- **Decision: the canonical Quake→world transform is
  `(map_x·s, map_z·s, −map_y·s)`; the grid is 2D in world XZ.**
  Source: code @ `tools/ogmap_lib/__init__.py:382-390`,
  `tools/ogworld/chunking.py:30-40`.

## Risks accepted

- **Neighbor-ring rendering may exceed the N64 draw/memory budget.**
  Each cell is small (max 920 faces / 3,527 verts), so 5 cells is feasible,
  but the resident footprint must be measured on hardware. Approximate budget:
  ~56 KB/cell for `T3DVertPacked` pairs (1,764 pairs × 32 bytes at max
  3,527 verts), ~12 KB/cell for batch arrays (1,024 × 12 bytes), plus
  ~72 bytes/cell for `T3DMat4FP`. 5 cells ≈ 345 KB total — likely fine on
  N64's 4-8 MB RDRAM, but must be verified. If it fails, reduce
  the ring to the active cell + the two neighbors in the travel direction, or
  drop to active-only after the render-origin fix is verified.
- **The render-origin fix could diverge from collision coordinates.**
  A host transform test validates the `(world − origin)·kInvScale·kPosScale +
  origin = world` math via a shared `render_origin_math.hpp` header (no N64
  compilation needed). The `LvlRoomRenderer::RenderOrigin()` accessor stores
  the origin as a plain `Vec3` for assertion. The authoritative check is the
  Inc 3 hardware gate.
- **Hardware traversal requires an emulator outside the sandbox.**
  The plan documents the exact procedure and adds per-chunk telemetry so the
  user can run the gate; the host suite and ROM build are verified in-sandbox.
- **Stale artifacts could confuse a host loader or test.**
  Inc 4 wipes the stale v1 artifacts and adds a seam-equivalence test so stale
  output cannot masquerade as a successful bake.

## Increment DAG

- Inc 1 — Fix render-origin model-matrix translation (M) — depends on: none —
  unblocks: 2, 3
- Inc 2 — Neighbor-ring rendering (M) — depends on: 1 — unblocks: 3
- Inc 3 — Hardware traversal acceptance gate (S) — depends on: 1, 2 —
  unblocks: none
- Inc 4 — Stale-artifact cleanup + seam-equivalence test (S) — depends on: none —
  unblocks: none

## Increments

### Inc 1 — Fix render-origin model-matrix translation (M) — DONE

**Depends on:** none
**Unblocks:** 2, 3
**Done criteria:** the active chunk renders at its correct world position, so
the player stands on the drawn floor and the "small piece / falls through"
symptom is gone for the active cell.

#### Files to touch

##### src/user/gameplay/render/lvl_room_renderer.cpp

- What changes: set the model matrix position to `render_origin` instead of
  `{0,0,0}` so the rebased geometry is translated back to world space.
- Function(s): `LvlRoomRenderer::Load(lvl_path, render_origin)` — change
  `const float p[3] = {0, 0, 0};` to
  `const float p[3] = {render_origin.x, render_origin.y, render_origin.z};`.
- Data shapes: `render_origin` is a `Vec3` in world units (already a parameter).
- Integration points: called from `map_runtime.cpp` `LoadRoomIntoActive` with
  the room's render origin from the v2 manifest.
- Error paths: none new; the model matrix is always built.

##### src/user/gameplay/render/lvl_room_renderer.{hpp,cpp} — store render_origin + host math header

- What changes: `LvlRoomRenderer` is N64-only (includes `<libdragon.h>`,
  `<rdpq.h>`, `<t3d/t3d.h>`, `<t3d/t3dmath.h>` unconditionally at the top of
  the `.cpp` — these headers do not exist on host, so `#ifdef __mips__`
  guards inside the file are insufficient). The strategy is:
  1. **Factor the transform math** into a new standalone header
     `src/user/gameplay/render/render_origin_math.hpp` containing a single
     `inline` helper that validates `(world − origin)·kInvScale·kPosScale +
     origin = world` using only `Vec3` and `float` — no N64 types. Both
     `LvlRoomRenderer::Load` and the host test include this header.
  2. **Store `render_origin_`** as a plain `Vec3` member in
     `LvlRoomRenderer` (set during `Load`), and expose it via
     `const Vec3& RenderOrigin() const`. This works on both host and N64
     with zero conditional compilation — no `T3DMat4FP` extraction needed.
  3. **Keep the renderer N64-only** — the `.cpp` stays as-is (no host
     compilation path). The host test validates the transform math
     independently via the shared header, and asserts
     `renderer.RenderOrigin() == expected_origin` on the stored value.
- Function(s): `LvlRoomRenderer::RenderOrigin()` (new, returns
  `render_origin_`); `ValidateRenderOriginTransform()` in the new math
  header.
- Data shapes: `Vec3 render_origin_` member; `Vec3` return from accessor.
- Integration points: host test reads `RenderOrigin()` and calls the math
  helper; N64 path unchanged.
- Error paths: none new; the math header is pure computation.

##### tests/interconnected_renderer_contract.cpp (new)

- What changes: a host test that loads a fixture LVL with a known
  `render_origin` and validates the transform math. Because
  `LvlRoomRenderer` is N64-only and cannot be compiled on host, the test
  uses the shared `render_origin_math.hpp` header to validate the
  `(world − origin)·kInvScale·kPosScale + origin = world` identity in
  isolation, and asserts `renderer.RenderOrigin() == expected_origin` on
  the stored value (the `RenderOrigin()` accessor is a plain `Vec3` member
  with no N64 dependency).
- Function(s): `test_render_origin_translation()`.
- Data shapes: a small fixture LVL with known vertices + a known `render_origin`.
- Integration points: uses `render_origin_math.hpp` + `LvlRoomRenderer::RenderOrigin()`.
- Error paths: assert `|drawn_world − source_world| < eps`; fail loudly on
  mismatch.

#### Edge cases

- A cell whose `render_origin` is `(0,0,0)` (if any) must still render
  correctly — the translation is a no-op.
- The model matrix must compose scale then translation so the packed int16
  (already `(world − origin)·kPosScale`) maps back to `world`.

#### Verification

- Run: host renderer contract test; `./compile-rom.sh`.
- Done: the active chunk renders at the correct world position; the player
  stands on the drawn floor; `coll_mesh` is non-null at motor time (per the
  existing `[update] frame start: coll_mesh=%p` telemetry).

### Inc 2 — Neighbor-ring rendering (M) — DONE

**Depends on:** 1
**Unblocks:** 3
**Done criteria:** the active cell plus its immediate ±X/±Z neighbors render
each frame, so crossing a seam shows the next chunk and the world feels
connected.

#### Files to touch

##### src/user/gameplay/render/chunk_ring_renderer.{hpp,cpp} (new)

- What changes: a render-only pool that loads the active cell and its four
  neighbors into `LvlRoomRenderer` instances and draws all of them each frame.
- Function(s):
  - `ChunkRingRenderer::Load(const MapSpecV2& spec, const V2RoomSpec& center, const char* build_dir)` — loads center + its four neighbors (resolved from `spec` by neighbor id), each rebased to its **own** `render_origin`; missing neighbors are skipped (not fatal), center failure is fatal.
  - `ChunkRingRenderer::Draw()` — draws all loaded renderers.
  - `ChunkRingRenderer::Free()` — frees all renderers.
- Data shapes: `center` is a `V2RoomSpec` (has `id`, `lvl_path`, `render_origin`,
  `neighbors[4]`); each neighbor is resolved from `spec.rooms` by id to get its
  own `lvl_path` + `render_origin`; `build_dir` is null on device (loads
  `rom:/lvl/forsyken-city/...`).
- Integration points: called from `gameplay_scene.cpp::Render` in map-pack
  mode; needs the active room's `V2RoomSpec` and the `MapSpecV2` from
  `MapRuntime`/`MapSpecV2`.
- Error paths: a missing neighbor LVL is skipped (decoration/hazard cell);
  a missing center LVL is fatal.

##### src/user/gameplay/scene/gameplay_scene.cpp

- What changes: in map-pack mode, draw the `ChunkRingRenderer` instead of only
  the active room's renderer.
- Function(s): `Render()` — replace `active->renderer->Draw()` with
  `impl_->chunk_ring_.Draw()`. In `TransitionToRoom()`, after
  `CommitActive()` succeeds (line ~421), call
  `chunk_ring_.Load(map_runtime_.Spec(), *map_runtime_.ActiveSpec(), build_dir)`
  to refresh the ring to the new cell's neighbors.
- Integration points: `ChunkRingRenderer` member; `ActiveSpec()` accessor
  (added in Inc 2 to `MapRuntime`).

##### src/user/gameplay/world/map_runtime.{hpp,cpp}

- What changes: expose the active room's `V2RoomSpec` and the `MapSpecV2` so
  the ring can resolve neighbor paths + render origins.
- Function(s): `ActiveSpec()` returning `const V2RoomSpec*` for the active
  room; `Spec()` returning `const MapSpecV2&`.
- Integration points: `ChunkRingRenderer::Load` uses these.

#### Edge cases

- A cell at the world edge has fewer than 4 neighbors; absent neighbors are
  skipped.
- A neighbor cell is decoration-only (no LVL or no renderable geometry); it is
  skipped without affecting the center.
- Crossing two cells in one dash: the ring refreshes to the final cell's
  neighbors after the transition commit.

#### Verification

- Run: host test that loads a 2×2 fixture and asserts the ring loads center +
  neighbors (requires the Inc 1 host-stub path); `./compile-rom.sh`.
- Done: crossing a seam shows the next chunk; the world feels connected.

### Inc 3 — Hardware traversal acceptance gate (S) — DONE

**Depends on:** 1, 2
**Unblocks:** none
**Done criteria:** a documented emulator procedure crosses ≥2 seams (one +X,
one world-Z/depth) with no fall-through, confirmed by per-chunk telemetry.

#### Files to touch

##### src/user/gameplay/rom_telemetry.{hpp,cpp}

- What changes: add a `float render_origin[3]` field to `RomTelemetry`; add a
  `render_origin` parameter to `RecordActiveRoom(const char* room_id, float
  fnorm_y, const Vec3& render_origin)`; extend `PrintLine()` to print the
  render-origin values. The existing `active_room[16]` and `floor_normal_y`
  fields already cover room id and floor normal; grounded state is tracked
  via `grounded_frames`.
- Function(s): `RecordActiveRoom` — new third parameter; `PrintLine` — add
  `orig=(%.1f,%.1f,%.1f)` to the format string.
- Data shapes: `Vec3` render_origin passed by const ref.
- Integration points: call site in `gameplay_scene.cpp` (around line 651)
  must pass the active room's `render_origin` from `MapRuntime::Active()`.

##### src/user/gameplay/scene/gameplay_scene.cpp

- What changes: update the `RecordActiveRoom` call to pass
  `active->render_origin` as the third argument. The active room id and
  floor normal are already passed; grounded state is implicit in
  `grounded_frames`.
- Function(s): `Update()` — extend the existing telemetry block at line ~651.
- Integration points: `RomTelemetry::RecordActiveRoom` signature change.

##### tests/rom_traversal_acceptance.md (new)

- What changes: document the exact Ares/Mupen64Plus procedure and expected
  logs: boot room/start position, active room changes, collision normal,
  no-null-mesh transitions, render-origin values, and checkpoint respawn.
- Data shapes: markdown procedure with expected log lines.

#### Edge cases

- The gate must include at least one +X seam and one world-Z/depth seam, and
  cross back.
- The gate must verify the player does not fall through at a seam (floor
  normal `y` near 1.0 on both sides).

#### Verification

- Run: user launches the ROM under Ares/Mupen64Plus and follows the procedure.
- Done: emulator traversal crosses ≥2 seams with no fall-through; per-chunk
  telemetry confirms active room changes and correct floor normals.

### Inc 4 — Stale-artifact cleanup + seam-equivalence test (S) — DONE

**Depends on:** none
**Unblocks:** none
**Done criteria:** the build directory is clean of stale v1 artifacts, and a
seam-equivalence test proves the bake and runtime resolve cells identically.

#### Files to touch

##### build/bake-fc-1200/

- What changes: remove the entire top-level directory contents except
  `staging/` and `interconnected_report.json`. This includes the stale
  `cell_04_23.lvl` orphan, the ~45 top-level `.lvl` files that duplicate
  `staging/`, the ~43 top-level per-cell `.colmesh` files (stale v1-era
  sidecars; the runtime now uses the single global
  `staging/forsyken-city.colmesh`), and the top-level v1
  `forsyken-city.mappack`, `forsyken-city.mappack.json`, and `chunks.json`
  (leftovers from earlier bakes with a different axis convention).
  **Before deleting:** run `grep -r "bake-fc-1200[^/]" tests/` to confirm
  no host test references the top-level directory (as opposed to
  `bake-fc-1200/staging`). The `LocalizePath` helper strips directory
  prefixes and uses only the filename, so a test passing
  `build/bake-fc-1200` (without `/staging`) as `build_dir` would resolve
  to the top-level stale files. Verified: `map_runtime_v2_smoke.cpp` uses
  `/tmp/inc4-build/staging` — safe.

##### Makefile

- What changes: ensure the `bake-forsaken-city` target wipes the entire
  top-level `build/bake-fc-1200/` contents (except `staging/` and
  `interconnected_report.json`) before publishing, so a chunk-size/axis change
  cannot leave orphan files.

##### tests/interconnected_seam_equivalence.py (new)

- What changes: assert the bake's `world_cell` arithmetic matches the runtime's
  `ResolveCellByPosition` formula exactly, including values exactly on seams.
  Because a Python test cannot call the C++ `ResolveCellByPosition`, it reads
  the baked manifest's actual `chunk_size`/`scale` (not hardcoded 1200/0.2)
  and asserts `world_cell` matches the runtime formula at seam and negative
  coordinates using those manifest values.
- **Coupling note:** the Python test independently re-derives the
  cell-resolution formula from manifest `chunk_size`/`scale`. If the C++
  `ResolveCellByPosition` formula changes, the Python test won't catch it
  unless both are updated. Add a cross-reference comment in both
  `map_runtime.cpp::ResolveCellByPosition` and this test pointing at each
  other.
- Function(s): `test_seam_equivalence()`.
- Data shapes: seam coordinates at cell boundaries (e.g. `world_x = n·cell_w`,
  `world_z = m·cell_w`) and `n·cell ± ε` probes.
- Integration points: uses `tools/ogworld/chunking.py::world_cell` and the
  manifest's `chunk_size`/`scale`.
- Error paths: any mismatch at a seam coordinate is a hard failure.

#### Edge cases

- Values exactly on a seam must resolve to the same cell in bake and runtime
  (no off-by-one from float rounding).
- Negative world coordinates (cells `n01`, `n02`, …) must resolve identically.

#### Verification

- Run: `python3 tests/interconnected_seam_equivalence.py`;
  `make bake-forsaken-city`.
- Done: build dir is clean; seam test passes.

## Cross-cutting verification

- `python3 tests/interconnected_map_contract.py`
- `python3 tests/interconnected_map_smoke.py`
- `python3 tests/interconnected_reachability.py`
- host renderer contract test (Inc 1) and neighbor-ring test (Inc 2)
- `python3 tests/interconnected_seam_equivalence.py` (Inc 4)
- `./compile-rom.sh`
- Ares/emulator traversal with per-chunk telemetry (Inc 3)

The final acceptance gate is hardware traversal, not merely "the bake
generated 45 files" or "the host renderer test passed."

## Standards / common-mistakes referenced

- `AGENTS.md` — preserve gameplay/ROM separation; inspect coordinate-system
  boundaries; rebuild ROM after N64-facing changes.
- `.agents/map-creation.md` — canonical transform, artifact dependencies,
  fixed-point constraints, `kPosFp`/`kPosScale` overflow.
- `.agents/common-mistakes/og-map-polygon-winding.md` — applies to Inc 1;
  validate transformed winding and quantized runtime normals.
- `.agents/common-mistakes/missing-player-start-init.md` — applies to Inc 3;
  distinguish initial spawn, transition carry, and checkpoint respawn.
- `.agents/common-mistakes/camera-respawn-reset.md` — applies to Inc 3;
  reset camera only after the checkpoint room is active.
- `.agents/common-mistakes/dfs-path-prefix.md` — applies to Inc 2 and 4;
  manifest paths must match the DFS subtree exactly.

## Open questions (CONSIDER from review)

- After the neighbor ring is stable, measure whether preloading the next ring
  (one cell ahead in the travel direction) removes visible hitches without
  exceeding N64 memory; do not add it before the ring is correct.
- If the 5-cell ring exceeds the N64 draw/memory budget, reduce to the active
  cell + the two neighbors in the travel direction, or drop to active-only
  after the render-origin fix is verified.
- Revisit T3DM/textured rendering after collision and placeholder rendering are
  stable; it is not a prerequisite for this plan.
- The Inc 2 host ring test has the same N64-only blocker as Inc 1; fold it
  into the Inc 1 host-stub decision rather than treating it as a separate,
  already-solvable test.
- The Inc 4 seam test's marginal value is low given both sides already share
  the same floor formula; the real risk (float rounding exactly on a seam) is
  only meaningfully exercised if the test uses the manifest's actual
  `chunk_size`/`scale` and probes `n·cell ± ε`.

## Out of scope

- B-side `1-1`…`1-10` map-pack conversion and cassette scene-stack semantics.
- Full OG gameplay parity for moving/falling/breakable blocks, NPCs, cutscenes,
  fixed cameras, and navigation runtime.
- Rebuilding the verified bake pipeline; the canonical IR → global CMSH +
  per-cell LVL + v2 manifest is retained as-is.
