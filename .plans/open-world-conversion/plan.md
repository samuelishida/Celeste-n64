# Fixup: Per-Chunk Colmesh Winding + Query ABI (persistent fall-through)

## Context

The `interconnected-map-fixup` plan marked all four increments DONE, and the
partitioning work it did is genuinely complete and verified in the codebase:
`cell_of` uses world-space XZ (`brush_grid.py:77-93`), the runtime
`ResolveCellByPosition` agrees (`map.cpp:211-221`), `chunk_size=1200` yields 47
cap-fitting chunks, the manifest `start_spawn` boot override is in place
(`gameplay_scene.cpp:384-397`), `--reuse-colmesh` is opt-in, and
`RaycastRoomMesh` carries `__attribute__((noinline))` (`world.cpp:136`).

**The ROM still falls through on traversal.** Re-running the fixup's own
"success" check — Ares boot shows `grounded=1` for 300 frames in `cell_00_00`
only — is the tell: that is a single chunk, static, no movement. The fixup
*localized* the boot failure to `cell_00_00` and made that one chunk land; it did
not validate the other 46 chunks or any seam crossing.

Auditing the nine prior plans in `.plans/` shows a consistent gap across the
entire pipeline history: **the colmesh triangle winding / transformed-normal
direction is validated only at bake time against the writer's own convention,
never against the runtime query across all chunks.** Three pieces of the
current runtime code converge on the same failure mode:

1. `RaycastRoomMesh` (`world.cpp:143`) deliberately calls
   `RaycastMesh(..., BackfaceCull::Ignore)` then re-applies
   `if (dot >= 0.0f) return GroundHit{}` (`world.cpp:150-152`). The
   `BackfaceCull::Ignore` + dot-filter shim was added because inverted-winding
   triangles were observed in the wild — the runtime *tolerates* either
   winding, then rejects by normal direction.
2. `-ffast-math` (from libdragon's shared flags; the `world.cpp:133-134`
   comment names it) makes the `dot >= 0.0f` comparison non-IEEE. A floor
   triangle with a borderline normal (near `+Y`, ray `{0,-1,0}`) can flip from
   pass to reject, dropping the floor.
3. Only `RaycastRoomMesh` (the leaf) has `noinline`. The struct-returning
   callers `RaycastRoomSource` (`world.cpp:232`), `QueryFloorSource` (`:281`),
   `QueryCeilingSource` (`:285`), and `ProbeFloorDebug` (`:299`) do **not**.
   `RaycastRoomSource` is a large function and a prime inlining candidate —
   the one-function noinline patched one call site, not the chain.

The boot "fix" worked because `cell_00_00`'s floor normal happened to land on
the surviving side of all three filters. Other chunks will not. This plan
root-causes the winding/ABI interaction and validates colmesh per-chunk
end-to-end, instead of re-doing the already-done partitioning.

### What is explicitly NOT redone

- `cell_of` axis / `scale` threading — already correct (`brush_grid.py:77`).
- `ResolveCellByPosition` — already the reference (`map.cpp:211`).
- `chunk_size=1200` default, Makefile vars, wipe guard — already done.
- Manifest `start_spawn` boot override — already done (`gameplay_scene.cpp:384`).
- `--reuse-colmesh` opt-in — already done (`bake_map_pack.py`).
- Z-axis regression / `ParseCellId` / seam test for `k ∈ {0,2,3}` — already done
  (`map_runtime_smoke.cpp:42,116,136`).

## Architectural decisions

- **Decision: validate colmesh winding against the runtime query, not the
  writer's convention.** The bake-side guardrail
  (`level_bake_report_smoke.py::reversed_winding_faces=0`) checks the writer's
  own triangulation; it cannot catch a winding that is self-consistent at bake
  time but whose transformed normal points the wrong way for the runtime's
  `dot >= 0.0f` filter. The audit in Inc 1 loads each `.colmesh` with the real
  runtime `LoadCollMesh` + `RaycastMesh` and asserts the normal the runtime
  would actually compute. Alternatives rejected: extending the bake-side
  guard (same blind spot); trusting the `BackfaceCull::Ignore` shim (it is the
  thing masking the bug).

- **Decision: the fix in Inc 2 is chosen by Inc 1's data, not picked now.**
  Three candidate fixes exist — (a) correct the bake writer's winding for the
  failing chunks, (b) harden the `dot >= 0.0f` filter with an epsilon
  (`dot > kEps` reject, so borderline `+Y` floors survive `-ffast-math`), (c)
  apply `noinline` to the full struct-returning query chain. They are not
  mutually exclusive but they target different layers; picking blind risks
  re-papering the symptom (as the fixup's single-function noinline did). Inc 1
  distinguishes: if the audit shows whole-chunk inverted normals → (a); if
  normals are correct but borderline `dot ≈ 0` → (b); if host audit passes but
  ROM fails → (c) is the real ABI corruption. Alternatives rejected: applying
  all three preemptively (hides which one mattered); noinline-only again
  (already tried at one site).

- **Decision: per-chunk ROM telemetry prints `floor_normal.y`, not just
  `grounded`.** The current `PrintLine` (`rom_telemetry.cpp:17`) prints
  `grounded_frames` (a cumulative counter); `RecordSurfaceSample` receives
  `ground_normal_y` (`rom_telemetry.cpp:~110`) but only thresholds it for
  `slope_ground_count` and never prints it. A fall-through on chunk N is
  invisible in telemetry today. Inc 3 adds `floor_normal.y` (and the active
  room id) to the telemetry line so a traversal fall-through shows a normal
  flip / zero, localizing the failure to the chunk. Alternatives rejected: a
  new debug console command (heavier, not needed for a one-field add).

## Assumptions and answers from code

- Decision: colmesh stores **quantized int16** vertices, no stored normals;
  runtime computes `Cross(b-a, c-a)` and flips to face the ray. Source: code @
  `docs/colmesh_format.md`, `src/user/gameplay/physics/geom.cpp:66-100`,
  `src/user/gameplay/physics/coll_mesh.cpp:~182`.
- Decision: bake writer applies `transform_point(v, scale) = (x*s, z*s, -y*s)`
  to vertices **before** writing; normals are not written. Source: code @
  `tools/writers/colmesh_writer.py::generate_triangles`,
  `tools/ogmap_lib/__init__.py:382-387`.
- Decision: the known winding-bug class is documented; the writer reverses
  the sorted order to "fix" it. Source:
  `.agents/common-mistakes/og-map-polygon-winding.md`,
  `tools/ogmap_lib/brush_geom.py:103`.
- Decision: only `RaycastRoomMesh` has `noinline`; the four other
  struct-returning query functions do not. Source: code @
  `src/user/gameplay/world/world.cpp:137,232,281,285,299`.
- Decision: host-side `LoadCollMesh` uses `fopen(path, "rb")` — works on host
  with no DFS shim. Source: code @ `src/user/gameplay/physics/coll_mesh.cpp:114`.
- Decision: host tests already load + raycast `.colmesh` files directly.
  Source: `tests/coll_mesh_query_test.cpp:59`, `tests/first_room_query_test.cpp:18`,
  `tests/player_motor_collmesh_test.cpp:147`.
- Decision: `CollisionQueryDiagnostics` exists but is not surfaced to ROM
  telemetry. Source: `src/user/gameplay/world/world.hpp:71`,
  `src/user/gameplay/world/world.cpp:320`, `src/user/gameplay/rom_telemetry.hpp`.
- Decision: current telemetry prints every 60 frames; `floor_normal` is never
  printed. Source: `src/user/gameplay/rom_telemetry.cpp:17`,
  `src/user/gameplay/scene/gameplay_scene.cpp:716`.
- Decision: `kMaxRooms=64`, `kMaxFaces=1024`, `kMaxVertices=8192`. Source:
  `src/user/gameplay/world/mappack_loader.hpp:33`,
  `src/user/gameplay/world/level_loader.hpp:24-25`.

## Risks accepted

- **The fix may be layered.** Inc 1 may show some chunks with inverted winding
  (writer fix) AND a borderline-`dot` ABI component (epsilon/noinline). Risk:
  fixing only one layer leaves residual fall-through. Mitigation: Inc 1 reports
  per-chunk pass/fail with the *reason* (inverted normal vs borderline `dot`),
  so Inc 2 can apply more than one fix; Inc 3's telemetry re-verifies in ROM.
- **`-ffast-math` is not in the project Makefile** (`Makefile:15` is `-Os`
  only); it comes from libdragon's shared flags. A future libdragon bump could
  change the behavior. Mitigation: Inc 2's epsilon fix (if chosen) is robust
  to `-ffast-math` regardless of source; the noinline route is belt-and-braces.
- **Host audit passes but ROM still fails.** If the winding is correct and
  `dot` is not borderline, the failure is pure MIPS ABI corruption that the
  host cannot reproduce. Mitigation: Inc 3's per-chunk ROM telemetry is the
  device-side oracle; Inc 2's noinline-on-full-chain fix is the response.

- Inc 1 — Per-chunk colmesh winding audit (host) (S) — depends on: none —
  unblocks: 2
- Inc 2 — Root-cause fix: winding / dot-epsilon / noinline chain (M) —
  depends on: 1 — unblocks: 3
- Inc 3 — Per-chunk ROM telemetry: floor_normal + active room (S) —
  depends on: 2 — unblocks: 4
- Inc 4 — `find_optimal_chunk_size` additive helper (S, optional) —
  depends on: 1 — unblocks: none (can run in parallel with 2, 3)

## Increments

### Inc 1 — Per-chunk colmesh winding audit (host) (S)
**Depends:** none
**Unblocks:** 2
**Done criteria:** a new host test loads every `.colmesh` in
`filesystem/lvl/forsyken-city/`, raycasts `{0,-1,0}` from above each
`MAT_SOLID` triangle's centroid, and asserts the runtime-computed normal has
`y > 0.5` (floor-upward). The test **runs**, and either passes for all chunks
or prints a per-chunk, per-face failure report naming the reason (inverted
normal `y < -0.5` vs borderline `|y| <= 0.5`). The report localizes the
breakage that Inc 2 targets.

#### Files to touch

##### tests/colmesh_winding_audit.cpp  (NEW)
- What changes: new host test. Iterate `filesystem/lvl/forsyken-city/*.colmesh`
  via `opendir`/`glob`; for each file, `LoadCollMesh(path)`; for each
  `CollTriangle t` where `t.material & MAT_SOLID`, first compute the geometric
  normal `n = Cross(b-a, c-a)` and normalize; only if `n.y > 0.5` (floor-upward)
  proceed to cast a down-ray from `centroid + (0, +small, 0)` with
  `RaycastMesh(mesh, origin, {0,-1,0}, max_t, BackfaceCull::Ignore)`, and
  assert `hit && normal.y > 0.5`. On failure print
  `chunk=%s face=%d material=%d normal=(%g,%g,%g) dot=%g reason=%s` where reason is one of
  `inverted` (`y < -0.5`), `borderline` (`|y| <= 0.5`), `miss` (no hit).
- Function(s): `int main(int argc, char** argv)` taking an optional
  `--dir=<path>` (default `filesystem/lvl/forsyken-city`); helper
  `bool audit_chunk(const char* path, int* fail_inverted, int* fail_borderline,
  int* fail_miss)`.
- Data shapes: reuses `physics::CollMesh`, `RaycastMesh`, `MAT_SOLID` from
  existing headers — no new types.
- Integration points: links the same `coll_mesh.cpp` + `geom.cpp` the runtime
  uses (host build), so the normal it computes is byte-for-byte the runtime
  normal (modulo MIPS `-ffast-math`, which the host does not apply — this is
  the deliberate blind spot Inc 3 covers).
- Error paths: `LoadCollMesh` returns null → report `chunk=%s load_failed` and
  continue (do not abort the whole run; one bad chunk should not mask others).

##### tests/CMakeLists.txt or the smoke build script
- What changes: add `colmesh_winding_audit` to the host build. If the project
  uses a plain `g++` one-liner (see `AGENTS.md` smoke section), document the
  command in the test header; no CMake needed.
- Error paths: none.

#### Edge cases
- Degenerate triangles (zero-area) where `Cross` is near-zero: classify as
  `borderline` and report — a degenerate floor triangle is itself a bug.
- Chunks with no `MAT_SOLID` triangles (decoration-only, no `.colmesh`): the
  glob will not find them (no file) — skip silently; do not fail the audit.
- A chunk whose floor is not axis-aligned (ramps): `normal.y > 0.5` is the
  floor-upward threshold (≈30° from vertical); ramps steeper than that are
  walls and should not be counted as floor failures. Use `y > 0.5` strictly.

#### Verification
- Run (host, no toolchain):
  ```sh
  g++ -std=c++17 -Isrc/user tests/colmesh_winding_audit.cpp \
    src/user/gameplay/physics/coll_mesh.cpp src/user/gameplay/physics/geom.cpp \
    -o /tmp/colmesh_winding_audit && /tmp/colmesh_winding_audit
  ```
- Tests to add/update: this IS the new test.
- Done: the audit runs over all 43 `.colmesh` files and prints a per-chunk
  pass/fail summary; if any chunk fails, the reason is identified so Inc 2
  knows whether to fix the writer (inverted), the filter (borderline), or the
  ABI (host passes, ROM fails → Inc 3 localizes).

### Inc 2 — Root-cause fix: winding / dot-epsilon / noinline chain (M)
**Depends:** 1
**Unblocks:** 3
**Done criteria:** the Inc 1 audit passes for all chunks on the host AND the
fix chosen below is applied. The choice is driven by Inc 1's report:
- **If Inc 1 shows `inverted` failures** → fix the bake writer's winding for
  those chunks (path A).
- **If Inc 1 shows `borderline` failures** → harden the `dot >= 0.0f` filter
  in `RaycastRoomMesh` to `dot > kDotEps` with a small positive epsilon so a
  `+Y` floor under `-ffast-math` survives (path B).
- **If Inc 1 passes on host but the ROM still falls through** → the failure is
  MIPS ABI; apply `noinline` to the full struct-returning query chain (path C).
- Multiple paths may apply; apply all that Inc 1's data supports.

#### Files to touch

##### tools/writers/colmesh_writer.py  (path A only)
- What changes: if Inc 1 shows whole-chunk inverted normals, the
  `sort_vertices_ccw` / `reversed(...)` interaction in `brush_geom.py:103` is
  producing the wrong sign for those chunks' face normals after transform.
  Re-derive the winding from the transformed face normal (not the map-space
  one) for the failing brush classes, or flip the fan order for those chunks.
- Function(s): `generate_triangles` / the winding helper it calls.
- Data shapes: unchanged (same `CollTriangle`).
- Integration points: the bake; re-run `make bake-forsaken-city` after the
  change (the Makefile already wipes the out dir).
- Error paths: the bake-side guardrail `reversed_winding_faces=0` must still
  hold; if it trips, the fix over-corrected.

##### src/user/gameplay/world/world.cpp  (path B and/or C)
- What changes:
  - (B) In `RaycastRoomMesh` (`world.cpp:150-152`), change
    `if (dot >= 0.0f) return GroundHit{};` to
    `if (dot > kBackfaceDotEps) return GroundHit{};` with
    `kBackfaceDotEps = 1e-3f` (or a value justified by Inc 1's borderline
    distribution). Rationale: under `-ffast-math`, a floor triangle with
    `dot == 0` (normal exactly `+Y`, ray `{0,-1,0}` gives `dot = -1`; a near-
    floor tilted triangle can give `dot` near 0 from above) can flip; a small
    reject-epsilon makes the filter robust. **Verify the sign direction
    against Inc 1's data before committing** — the reject condition must keep
    real floors.
  - (C) Apply `__attribute__((noinline))` to `RaycastRoomSource`
    (`world.cpp:232`), `QueryFloorSource` (`:281`), `QueryCeilingSource`
    (`:285`), and `ProbeFloorDebug` (`:299`). Keep the existing attribute on
    `RaycastRoomMesh`. Add a one-line comment on each pointing at the
    `world.cpp:133-134` rationale.
- Function(s): `RaycastRoomMesh`, `RaycastRoomSource`, `QueryFloorSource`,
  `QueryCeilingSource`, `ProbeFloorDebug`.
- Data shapes: unchanged.
- Integration points: every caller of floor/wall/ceiling queries — motor,
  camera, respawn. The noinline attributes are transparent to callers.
- Error paths: none at runtime; a mis-signed epsilon (path B) would reject
  real floors — Inc 3's telemetry catches this in ROM before it ships.

##### tests/colmesh_winding_audit.cpp
- What changes: after the fix, the audit must pass for all chunks. If path A
  required a re-bake, point the audit at the re-baked `filesystem/lvl/` (it
  already globs the live dir, so a `make bake-forsaken-city` is enough).
- Error paths: if the audit still fails after the fix, do not mark Inc 2 done;
  the root cause is not what Inc 1 suggested — revisit.

#### Edge cases
- Path A changes chunk geometry content; every chunk file changes even where
  ids coincide. The Makefile wipe guard (`Makefile:132`) handles this; force a
  clean re-bake.
- Path B's epsilon must not reject ramps that are legitimate floors (slope <
  ~30°). `kBackfaceDotEps = 1e-3f` rejects only `dot > 1e-3`; it passes
  `dot <= 1e-3`, including real floors and borderline values. Ramps with
  `dot < -0.5` are unaffected. Confirm against `docs/movement_spec.md` max
  walkable slope if it specifies one.
- Path C increases code size slightly (five out-of-line functions); N64 I-cache
  pressure is a non-issue at this scale.

#### Verification
- Run:
  ```sh
  # after path A (re-bake) + B/C:
  /tmp/colmesh_winding_audit           # host audit green
  g++ -std=c++17 -Isrc/user tests/map_runtime_smoke.cpp ... -o /tmp/map_runtime_smoke
  /tmp/map_runtime_smoke /tmp/bake-fc-1200/forsyken-city.mappack /tmp/bake-fc-1200
  ./compile-rom.sh
  ```
- Tests to add/update: Inc 1 audit (must pass); `map_runtime_smoke` (unchanged,
  must still pass); `coll_mesh_query_test`, `first_room_query_test`,
  `player_motor_collmesh_test` (must still pass — the fix must not regress
  existing single-room queries).
- Done: host audit green for all 43 `.colmesh` files; existing host smokes green;
  ROM built. Inc 3 verifies the ROM side. The map-pack may contain 47 total
  grid chunks, including chunks without a colmesh file.

### Inc 3 — Per-chunk ROM telemetry: floor_normal + active room (S)
**Depends:** 2
**Unblocks:** 4
**Done criteria:** the ROM telemetry line includes the active room id and the
current `floor_normal.y`; under Ares, walking across ≥ 2 chunk boundaries (one
X, one Z) shows the room id changing and `floor_normal.y` staying `> 0.5` (or
reporting the chunk where it drops — localizing any residual fall-through).

#### Files to touch

##### src/user/gameplay/rom_telemetry.{hpp,cpp}
- What changes: add `char active_room[16]` and `float floor_normal_y` to the
  `RomTelemetry` record; `RecordSurfaceSample` already receives
  `ground_normal_y` (`rom_telemetry.cpp:~110`) — thread it into the stored
  field and print it in `PrintLine` (`rom_telemetry.cpp:17`). Add the active
  room id to the per-frame `RecordPlayerState` path (the caller,
  `gameplay_scene.cpp`, has `map_.ActiveRoomId()`). Capture the room id and
  floor-normal value from the same post-`RefreshContacts` tick and store them
  together; do not look up the room later when `PrintLine` runs. This keeps a
  seam-crossing room id paired with the normal sample that produced it, even
  though the printed 60-frame window may be stale.
- Function(s): `RecordPlayerState` (add `active_room` param),
  `RecordSurfaceSample` (store `ground_normal_y`), `PrintLine` (append
  `room=%s fnorm=%.3f`).
- Data shapes: `RomTelemetry` gains two fields; the printed line gains two
  tokens. No struct-layout-sensitive ABI (it's a debug struct).
- Integration points: `gameplay_scene.cpp:637-647` (per-frame record) and
  `:716-717` (print every 60 frames); immediately after `RefreshContacts`, pass
  `map_.ActiveRoomId()` and that tick's contact normal together. `PrintLine`
  only prints the stored pair.
- Error paths: if `active_room` is empty (no map loaded), print `room=-`; if
  no floor contact, print `fnorm=nan` or `fnorm=0` — either makes a
  fall-through visible as a non-`>0.5` value.

##### src/user/gameplay/scene/gameplay_scene.cpp
- What changes: pass the active room id and the player's current ground
  normal into the telemetry record calls from the same post-`RefreshContacts`
  update. The ground normal is already computed by `RefreshContacts` /
  `player_motor`; surface it alongside the room id so both fields describe the
  same tick.
- Function(s): the per-frame update that calls `telemetry.RecordPlayerState`.
- Error paths: none.

#### Edge cases
- The ground normal is only meaningful when `grounded`; when airborne, print
  the last known normal or `0` — the telemetry is diagnostic, not authoritative.
- Printing every 60 frames may skip a fast chunk transition; for the
  traversal verification, temporarily lower the print interval (or add a
  one-shot print on room change) so the seam crossing is captured. A
  `room-changed` one-shot debugf in `SetActivByPosition` is the lightest option.

#### Verification
- Run: `./compile-rom.sh`; launch in Ares; walk East then South across two
  boundaries; capture the telemetry log.
- Tests to add/update: no host test (telemetry is ROM-only); the Inc 1 + Inc 2
  host audits are the gate. Optionally extend `rom_telemetry_test.cpp` to
  assert the new fields exist in `PrintLine` output (host build of the
  telemetry struct).
- Done: telemetry shows `room=cell_00_00 fnorm=...>0.5` at boot, the room id
  changes as the player crosses, and `fnorm` stays `> 0.5` across the seam —
  or the failing chunk is named in the log.

### Inc 4 — `find_optimal_chunk_size` additive helper (S, optional)
**Depends:** 1; can run in parallel with Inc 2 and Inc 3
**Unblocks:** none
**Done criteria:** `bake_map_pack.py` accepts `--auto-chunk-size` which, when
  passed, searches `chunk_size` candidates (e.g. 800–2000 step 50) and picks
  the smallest that fits `kMaxRooms`/`kMaxFaces`/`kMaxVertices`; the default
  behavior (no flag) is unchanged (`1200` for `1.map`).

#### Files to touch

##### tools/bake_map_pack.py
- What changes: add `--auto-chunk-size` (store_true); when set, call a new
  `find_optimal_chunk_size(parsed_map, scale)` that runs
  `partition_parsed_map` at each candidate and returns the best fit. The
  existing fatal cap/room-count guards are the oracle for "fits".
- Function(s): `find_optimal_chunk_size(parsed_map, scale) -> float`.
- Data shapes: unchanged.
- Integration points: `main`, called before the real `partition_parsed_map`.
- Error paths: no candidate fits → fatal with the "tightest" failure report
  (smallest room count over `kMaxRooms`, smallest face count over `kMaxFaces`).

#### Edge cases
- The search re-parses the map per candidate; for `1.map` this is cheap
  enough. If it is slow, cache the `ParsedMap` and only re-partition.
- Must not change the default — `--auto-chunk-size` is opt-in, so existing
  bakes and CI are unaffected.

#### Verification
- Run: `python3 tools/bake_map_pack.py assets/og_converted/maps/1.map
  --auto-chunk-size --out-dir /tmp/bake-auto` → expect a chunk_size near 1200
  and 47 chunks.
- Tests to add/update: `tests/map_pack_smoke.py` — one case asserting
  `--auto-chunk-size` picks a fitting size; the existing 1200 default cases
  unchanged.
- Done: opt-in auto-tune works and agrees with the manually-validated 1200.

## Cross-cutting verification

- After Inc 2 + Inc 3: host `colmesh_winding_audit` green for all 43 `.colmesh`
  files (within the 47-chunk map-pack);
  host `map_runtime_smoke`, `coll_mesh_query_test`, `first_room_query_test`,
  `player_motor_collmesh_test` green; ROM built. Under Ares, walk the loop
  `cell_00_00` → East `cell_01_00` → South `cell_01_n01` → West `cell_00_n01`
  → North `cell_00_00`; telemetry shows the room id changing and
  `floor_normal.y > 0.5` (or names the failing chunk) at every step. No
  fall-through at any seam.

## Standards / common-mistakes referenced

- `.agents/common-mistakes/og-map-polygon-winding.md` — applies to: Inc 1, Inc
  2 path A. The known winding-bug class; Inc 1 is the first test that checks
  it against the runtime query, not just the writer.
- `.agents/common-mistakes/missing-player-start-init.md` — applies to: Inc 3
  (boot is already correct from the fixup; Inc 3 only adds diagnostics, no
  spawn reset change).
- `.agents/common-mistakes/dfs-path-prefix.md` — applies to: Inc 2 path A
  (re-baked chunks stay under `filesystem/lvl/forsyken-city/`, loaded as
  `rom:/lvl/forsyken-city/<chunk>.lvl` — unchanged).
- `AGENTS.md` "Agent working rules" — inspect coordinate-system boundaries
  first; this plan inspects the *normal-direction* boundary, which the
  partitioning fix did not reach.

## Open questions (CONSIDER from review)

- **Is `-ffast-math` actually present in the N64 build?** It is not in the
  project `Makefile:15` (`-Os` only); the `world.cpp:133` comment asserts it
  comes from libdragon's shared flags. Inc 2 path B/C is robust either way,
  but confirming the flag's source (and whether it can be disabled for this
  one translation unit) would let us choose between epsilon (B) and noinline
  (C) more deliberately. Verify with `make V=1` and inspect the compiler
  invocation for `world.cpp`.
- **Should `RaycastMesh` store a normal per triangle at bake time?** Storing
  the (quantized) normal would eliminate the runtime `Cross` and the
  `-ffast-math` sensitivity entirely, at the cost of 12B/triangle. Out of
  scope for this plan (the format is frozen and shipped across 47 chunks), but
  worth considering if the ABI fix proves fragile across libdragon bumps.
- **Per-room checkpoint spawns** (carried CONSIDER from the fixup) — still
  unresolved; orthogonal to this plan.

## Out of scope

- Re-doing the partitioning axis, `chunk_size`, `start_spawn` boot, or
  `--reuse-colmesh` — all already done by `interconnected-map-fixup` and
  verified present in the codebase.
- T3DM renderer cutover, moving-platform `.nav` runtime — unchanged.
- A colmesh format change (stored normals) — noted as CONSIDER only.
- A per-room checkpoint spawn manifest field — carried CONSIDER, orthogonal.
