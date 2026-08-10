# Fixup: Interconnected Map Regression (fall-through + wrong chunks)

## Context

The `whole-interconnected-map` plan shipped (all increments marked DONE), but
the ROM regressed: the map-pack boots (60 rooms, start `cell_00_00`), yet on
the very first frame the player is teleported to `cell_00_n01` and falls
through the floor forever (velocity ramps `-10 … -120`, never lands). This
fixup plan restores the traversable whole-A-side map.

Root-cause investigation (this session, empirically validated with scratch
bakes — repo left untouched):

1. **Primary bug — `cell_of` partitions on the wrong axis.**
   `tools/ogmap_lib/brush_grid.py::cell_of` computes
   `(floor(map_x/cs), floor(map_z/cs))`, but in Quake `.map` coords **map_z is
   the UP axis**. The runtime `Map::ResolveCellByPosition`
   (`src/user/gameplay/world/map.cpp`) resolves the player's **world**
   position with `(floor(world_x/(cs·scale)), floor(world_z/(cs·scale)))`,
   where `world = transform_point(map) = (map_x·s, map_z·s, −map_y·s)`. So the
   bake's second grid axis (map_z/up) and the runtime's second grid axis
   (world_z = −map_y/depth) are **different axes**. Every chunk's geometry,
   adjacency graph, and spawn placement is therefore in cells that do not
   match the runtime's cell resolution. When the player boots at
   `(0, 108.8, −9.6)`, the runtime resolves it to `cell_00_n01`, but that
   chunk's baked colmesh covers `map_z ∈ [−650, 0)` — the player's
   `map_z = 544` is outside it → **no floor → fall-through**. The X axis
   agrees between the two systems, which is exactly why the host smoke tests
   (which only drive movement along +X) pass while the ROM is broken.

2. **`chunk_size=650` was tuned against the buggy partition.** With the
   corrected axis, `650` yields **118 non-empty cells > `kMaxRooms` (64)** and
   the bake fatal-errors. The corrected axis needs a larger chunk size (see
   Increment 1 — `1200` is the sweet spot).

3. **Boot spawn selection gap (masked by #1).** `LoadLevelInto`
   (`level_loader.cpp`) sets `room.player_start` = the **last** PlayerSpawn in
   the room's `.lvl` (loop overwrite). The `.lvl` writer emits all point-entity
   spawns in partition order; the manifest's correct `start_spawn` (the
   `Start` spawn, world `(0, 25.6, 89.6)`) is populated but **never used** at
   boot. In the buggy bake, `cell_00_00`'s last PlayerSpawn was the "Tutorial"
   spawn `(0, 108.8, −9.6)` → the player boots in mid-air. After fix #1 the
   `Start` spawn is alone in `cell_00_00`, so this becomes correct as a side
   effect — but the boot should use the manifest `start_spawn` to be robust.

4. **Stale-colmesh reuse hazard.** `bake_map_pack.py::write_chunk` reuses an
   existing colmesh when the writer emits none
   (`elif colmesh_path.exists()`), with hardcoded `triangles=944`. After a
   chunk-size/axis change, old colmesh files with coincidentally-matching ids
   could be silently mixed into a new bake. Must become opt-in.

### Empirical validation (scratch, reverted)

- Patched `cell_of` → `(floor(x/cs), floor(−y/cs))`, re-baked `1.map`:
  | chunk_size | rooms | max faces | max verts | notes |
  |---|---|---|---|---|
  | 650 | **118** | — | — | fatal: > `kMaxRooms` 64 |
  | 1000 | **67** | — | — | fatal: > 64 |
  | 1100 | 58 | 856 | 3328 | OK, room count tight |
  | **1200** | **47** | **891** | **3412** | **chosen** |
  | 1250 | 47 | 997 | 3824 | OK |
  | 1300 | 46 | 1008 | 3866 | OK, faces tight |
  | 1500/1800 | — | 1326/1524 | — | fatal: face cap in `cell_n01_n02` |
- Containment holds: summed chunk brushes == whole-map brushes (1182).
- Start spawn resolves to `cell_00_00` in **both** bake and runtime.
- Floor probe: `cell_00_00.colmesh` has a floor ~19 units below the Start
  spawn (raycast HIT at `y=6.4`) — the player lands.
- Z-axis resolution now agrees with the bake (`cell_00_n01` at world `z < −240`,
  `cell_n02_00` at `x < −240`, etc.).
- `map_runtime_smoke` + `map_transition_smoke` pass against the corrected bake
  with the **unchanged** runtime; `cell_00_00` now reports
  `player_start=(0, 25.6, 89.6)` (the Start spawn).

## Architectural decisions

- **Decision: fix `cell_of` to partition by world-space XZ, using the exact
  arithmetic the runtime uses.** The runtime resolves
  `(floor(world_x/(cs·s)), floor(world_z/(cs·s)))`. The bake must compute cell
  indices with the **same formula**: transform the map point to world space
  (`transform_point`) and floor by `(chunk_size·scale)` — not a map-unit
  shortcut like `floor(−map_y/cs)`, because `scale=0.2` is not binary-exact and
  the two expressions can differ by one cell exactly at a seam (`map_y` an
  exact multiple of `cs`), re-introducing the same mismatch this plan fixes.
  `cell_of` therefore takes `scale` and every caller passes it. `cell_id` and
  `neighbor_cell` operate on cell keys and need no change; `world_aabb_for_cell`
  DOES change (its fallback encodes the old up-axis and its result is
  map-scaled, not world-space — see Inc 1). Alternatives rejected: changing
  the runtime to partition by map_z/up (wrong for a ground-plane world); the
  map-unit `(x, −y)` formula (FP seam hazard, rejected by review).

- **Decision: default `chunk_size` becomes `1200`** (was 650). Rationale: with
  the corrected axis it is the best fit — 47 rooms (safe margin under
  `kMaxRooms=64`) and max 891 faces / 3412 verts (safe margins under
  `kMaxFaces=1024` / `kMaxVertices=8192`). Alternatives rejected: 1100 (58
  rooms — too close to the room-table limit), 1300 (1008 faces — too close to
  the face cap), auto-tune (nice-to-have; the existing fatal guards already
  make a fixed default safe — see CONSIDER). The bake's existing cap/room-count
  guards remain the safety net.

- **Decision: boot positions the player from the manifest's `start_spawn`**
  (the `Start`-named PlayerSpawn), not from `room.player_start` derived from
  the `.lvl`'s last PlayerSpawn. Rationale: the manifest field is authored by
  `find_start_room` (which correctly looks for the entity named `Start`) and
  is the only robust source across bake re-runs. Chunk transitions keep using
  `LoadRoomGeometry` (player-position carry — unchanged).

- **Decision: colmesh reuse in `bake_map_pack.py` becomes opt-in**
  (`--reuse-colmesh`, default off). Rationale: silent reuse of a stale colmesh
  after a chunk-config change can ship wrong collision. The earlier
  session's workflow that relied on reuse can pass the flag explicitly.

## Assumptions and answers from code

- Decision: the bake originally partitioned in map units with `cell_of =
  (floor(x/cs), floor(z/cs))` (the bug); the fix makes it partition in world
  space exactly like the runtime (`floor(world/(cs·s))`). Source: code @
  `tools/ogmap_lib/brush_grid.py:44-53` and
  `src/user/gameplay/world/map.cpp:213-225`.
- Decision: `world = transform_point(map) = (map_x·s, map_z·s, −map_y·s)`.
  Source: code @ `tools/ogmap_lib/__init__.py:382-386`.
- Decision: `kMaxRooms=64`, `kMaxFaces=1024`, `kMaxVertices=8192`. Source:
  code @ `src/user/gameplay/world/mappack_loader.hpp` and
  `src/user/gameplay/world/level_loader.hpp:24-25`.
- Decision: `LoadLevelInto` sets `room.player_start` to the last PlayerSpawn
  in the `.lvl` (loop overwrite). Source: code @
  `src/user/gameplay/world/level_loader.cpp:247-249`.
- Decision: manifest `start_spawn` for the start room is correct
  `(0, 25.6, 89.6)`, `has_start_spawn=true`. Source: verified this session —
  `filesystem/lvl/forsyken-city/forsyken-city.mappack.json` and the corrected
  scratch bake.
- Decision: corrected axis + `chunk_size=1200` fits all caps and ≤ 64 rooms,
  containment holds (1182 == 1182). Source: verified this session (scratch
  bake, reverted).
- Decision: floor exists under the Start spawn in the corrected
  `cell_00_00` colmesh. Source: verified this session — raycast HIT at
  `y=6.4` from spawn `(0, 25.6, 89.6)`.
- Decision: host smokes pass against the corrected bake with the unchanged
  runtime. Source: verified this session — `map_runtime_smoke`,
  `map_transition_smoke` green.
- Decision: `cell_id`/`neighbor_cell` operate on cell keys and are
  axis-agnostic (no change needed); `world_aabb_for_cell` is NOT — its
  grid-aligned fallback maps the second index to the up axis (map_z) and its
  output is map-scaled rather than world-space, so it must change with the
  axis fix (see Inc 1). Source: code @ `tools/ogmap_lib/brush_grid.py`.
- Decision: `ResetPlayerToRoomStart` already calls
  `camera_controller.Reset(camera, player.position)` on the boot/respawn path
  (so no camera artifact from the Start-spawn snap). Source: code @
  `src/user/gameplay/scene/gameplay_scene.cpp:277` and `:632`.

## Risks accepted

- **New chunk boundaries.** `chunk_size=1200` changes where transitions fire
  vs the (broken) 650 bake. Mitigation: the guards keep chunks loadable; the
  transition seam is verified by the probes + ROM traversal; accept, revisit
  if hitches show.
- **Decoration-only chunks may legitimately lack colmesh.** The player could
  fall through areas that are only decorated (unchanged from before). Accept;
  those areas carry no gameplay floor by construction.
- **Per-room checkpoint spawns still use `.lvl`'s last PlayerSpawn.** After
  fix #1 this is correct for the start room (Start is alone in `cell_00_00`),
  but a checkpoint room with multiple PlayerSpawns could snap a respawn to the
  wrong spawn. The save's checkpoint position is snapped separately by
  `ResetPlayerToRoomStart`; verify in ROM, note as CONSIDER.
- **Boot-fix depends on the manifest `start_spawn` being fresh.** After Inc 1
  re-bakes, the manifest is regenerated, so the field is consistent. Accept.

## Increment DAG

- Inc 1 — Fix bake grid axis + retune chunk_size (M) — depends on: none —
  unblocks: 2, 3, 4
- Inc 2 — Boot player from manifest start_spawn (S) — depends on: 1 —
  unblocks: 4
- Inc 3 — Harden colmesh reuse (opt-in) (S) — depends on: 1 — unblocks: 4
- Inc 4 — Update tests/docs, rebuild ROM, emulator verification (M) —
  depends on: 1, 2, 3 — unblocks: none

## Increments

### Inc 1 — Fix bake grid axis + retune chunk_size (M)
**Depends on:** none
**Unblocks:** 2, 3, 4
**Done criteria:** `bake_map_pack.py` on `1.map` (default chunk_size 1200)
emits 47 cap-fitting chunks with containment (1182 brushes), start room
`cell_00_00`, `start_spawn=(0, 25.6, 89.6)`; every chunk's cell matches the
runtime `ResolveCellByPosition` for its content **including exactly at cell
seams**; `filesystem/lvl/forsyken-city/` is wiped and re-baked clean (no
old-axis chunks can leak into the DFS).

**STATUS: DONE.** `brush_grid.py::cell_of(point, chunk_size, scale)` now
partitions in world space with the runtime's exact arithmetic
(`floor((x·s, −y·s)/(cs·s))`); `scale` threaded through
`partition_parsed_map` / `entity_brushes_in_cell` / `world_aabb_for_cell`
(now world-space, second axis = depth) / `find_start_room` /
`build_chunk_submap`. `bake_map_pack.py` defaults to `--chunk-size 1200`
(was 1000); room-count guard message corrected (larger cells, not smaller).
Makefile: `FORSYKEN_CITY_CHUNK_SIZE ?= 1200`, out dir `build/bake-fc-1200`,
and the `bake-forsaken-city` recipe wipes `filesystem/lvl/forsyken-city/*`
before copying. Verified: bake emits 47 cap-fitting chunks (max 891 faces /
3412 verts), containment 1182 == 1182, start room `cell_00_00`,
`start_spawn=(0, 25.6, 89.6)`; `map_runtime_smoke` + `map_transition_smoke`
pass against the corrected bake (runtime unchanged). Fixed a latent test bug
exposed by the corrected axis: `map_runtime_smoke.cpp`'s `sscanf("cell_%d_%d")`
could not parse negative-index ids (`cell_n01_00`); added `ParseCellId`.
`filesystem/lvl/forsyken-city/` re-baked clean via `make bake-forsaken-city`
(47 `.lvl`, 43 `.colmesh`, manifest; no stale 650-axis chunks).

#### Files to touch

##### tools/ogmap_lib/brush_grid.py
- What changes: fix the grid axis in `cell_of` (map_z/up → world Z) AND make
  it operate in world space with the identical arithmetic the runtime uses
  (`floor(world / (chunk_size·scale))`), so no FP seam divergence is possible;
  update the module + function docstrings to state the world-XZ convention.
- Function(s):
  ```python
  def cell_of(point: Vec3, chunk_size: float, scale: float) -> CellKey:
      """Map-unit point -> world-space XZ grid cell.
      Transforms to world coords (transform_point: world = (x*s, z*s, -y*s))
      then floors by (chunk_size*scale) — byte-for-byte the same formula as
      the runtime Map::ResolveCellByPosition. Do NOT use map_z (the up axis)
      and do NOT use a map-unit shortcut (FP seam divergence)."""
      cell = chunk_size * scale
      wx = point[0] * scale
      wz = -point[1] * scale
      return (int(math.floor(wx / cell)), int(math.floor(wz / cell)))
  ```
- Data shapes: `CellKey = (ix, iz)` unchanged; `cell_of` gains a `scale`
  parameter (all callers pass it).
- Integration points: `partition_parsed_map(parsed_map, chunk_size, scale)`,
  `entity_brushes_in_cell(entity, cell_key, chunk_size, scale)`,
  `world_aabb_for_cell(parsed_map, cell_key, chunk_size, scale)` (via
  `cell_of`), `bake_map_pack.py::find_start_room` (add `scale` param).
- `world_aabb_for_cell` also changes: (a) its grid-aligned fallback must map
  the second index to **map_y (depth)**, not the up axis —
  `fallback_min = (ix*cs, iz*cs, -8192)`, `fallback_max = ((ix+1)*cs, (iz+1)*cs, 8192)`;
  (b) the returned AABB must be **world-space**, i.e. apply
  `transform_point` to both corners (currently it only multiplies by scale,
  leaving the AABB in map-scaled space with z=up — wrong for the runtime's
  preload culling). The real-bounds path (from brush AABBs) collects map
  (x,y,z) extents; transform those min/max corners with `transform_point` too.
- Error paths: none new — the existing cap/room-count fatal guards fire on any
  chunk config that no longer fits (e.g. someone passes 650 again → 118 cells
  → fatal, which is correct behavior).

##### tools/bake_map_pack.py
- What changes: change the `--chunk-size` CLI default from `1000` (current
  argparse default, `bake_map_pack(..., chunk_size: float = 1000.0)`) to
  `1200`; pass `scale` into `cell_of`/`find_start_room`/`partition_parsed_map`;
  update the header comment (it documents the 650 result).
- Function(s): `main`/argparse, `find_start_room(parsed_map, cell_map,
  chunk_size, scale)`, `partition_parsed_map(parsed_map, chunk_size, scale)`,
  `build_chunk_submap(parsed_map, entity_indices, cell_key, chunk_size, scale)`
  (threads `scale` into `entity_brushes_in_cell`).
- Data shapes: unchanged.
- Integration points: Makefile passes the value explicitly (Inc 1 Makefile
  change below).
- Error paths: unchanged.

##### Makefile
- What changes: `FORSYKEN_CITY_CHUNK_SIZE ?= 650` → `?= 1200`;
  `FORSYKEN_CITY_OUT_DIR ?= build/bake-fc-650` → `build/bake-fc-1200`; add a
  guard to the `bake-forsaken-city` rule that wipes
  `filesystem/lvl/forsyken-city/*` **before** copying fresh chunks, so any
  incremental `make` after this change is safe (old-axis chunk files with
  coinciding ids must not ship in the DFS).
- Function(s): none (variables + a `rm -rf`/`mkdir -p` guard in the recipe).
- Data shapes: n/a.
- Integration points: `bake-forsaken-city` target + DFS wildcard rules
  (unchanged patterns).
- Error paths: a stale `build/bake-fc-650/` must not leak — see cleanup below.

#### Edge cases
- A re-run with the old `--chunk-size 650` must now fatal with "Non-empty cell
  count 118 exceeds kMaxRooms" — this is the guard working, not a regression.
- Cell ids change meaning (z-index now = depth), so every chunk file changes
  content even where ids coincide (e.g. `cell_00_00`). The Makefile guard + a
  one-time manual wipe of `build/bake-fc-*` and
  `filesystem/lvl/forsyken-city/` make the first post-fix bake clean.
- Seam ties: a player exactly on a cell boundary resolves by the runtime's
  existing hysteresis (`SetActivByPosition` returns unchanged when the id is
  the active room), so a one-cell flip at an exact seam cannot thrash.

#### Verification
- Run:
  ```sh
  rm -rf build/bake-fc-650 build/bake-fc-1200 filesystem/lvl/forsyken-city
  python3 tools/bake_map_pack.py assets/og_converted/maps/1.map \
      --out-dir /tmp/bake-fc-1200 --chunk-size 1200 --mappack-id forsyken-city
  python3 - <<'PY'
  import json
  d=json.load(open('/tmp/bake-fc-1200/chunks.json'))
  assert d['chunk_count'] <= 64
  assert all(c['fits_caps'] for c in d['chunks'])
  assert sum(c['brush_count'] for c in d['chunks']) == 1182
  PY
  ```
- Boundary-equivalence test (MUST-FIX from review): host-side, for several
  `k`, assert bake `cell_of` on map points `(k·cs, k·cs, any)` equals the
  runtime `Map::ResolveCellByPosition` on the transformed world points
  `(k·240, y, −k·240)` — including values **exactly on** the seam, not just
  just-below. Add this as a case in `tests/map_runtime_smoke.cpp` (Inc 4
  wires it; run it here against the new bake).
- Tests to add/update: `tests/map_pack_smoke.py` (default 1200; face-cap
  guard becomes `--chunk-size 1500` which trips the face cap; room-count
  guard `--chunk-size 650` tripping `kMaxRooms`); `tests/mappack_smoke.py`
  (default 1200).
- Done: bake emits 47 chunks, all fit, containment holds, start room +
  `start_spawn` correct, bake/runtime resolution agree including at seams,
  and the filesystem subdir holds only new-axis chunks.

### Inc 2 — Boot player from manifest start_spawn (S)
**Depends on:** 1
**Unblocks:** 4
**Done criteria:** the ROM boots with the player at the `Start` spawn
`(0, 25.6, 89.6)` (snapped to the floor) regardless of `.lvl` entity order.

**STATUS: DONE.** `BootMapPack` in `gameplay_scene.cpp` now overrides
`room.player_start` / `room.checkpoint` from the manifest `start_spawn` when
booting the manifest's start room (guarded by `strncmp(spec.start_room_id,
map_.ActiveRoomId(), MapRoomSpec::kIdLen) == 0` and `has_start_spawn`),
before `ResetPlayerToRoomStart()`. The misleading "mismatch is handled by the
chunk transition system during the first frame" comment was replaced.
`tests/map_runtime_smoke.cpp` now asserts the manifest `start_spawn` equals
the world Start position `(0, 25.6, 89.6)` and that
`ResolveCellByPosition(start_spawn)` returns `start_room_id` — passes against
the corrected bake. ROM-level `[spawn] authored=(0.00,25.60,89.60)` check is
Inc 4.

#### Files to touch

##### src/user/gameplay/scene/gameplay_scene.cpp
- What changes: in `BootMapPack`, set `room.player_start` /
  `room.checkpoint` from the manifest `start_spawn` **only when booting the
  manifest's start room** and `has_start_spawn` is true, **before**
  `ResetPlayerToRoomStart()`; otherwise keep the current behavior (`.lvl`
  spawn). Remove the now-misleading "NOTE: Player spawn position mismatch is
  handled by the chunk transition system during the first frame" comment — the
  boot is supposed to be correct, not rely on a frame-1 transition.
- Function(s):
  ```cpp
  // inside BootMapPack, before ResetPlayerToRoomStart():
  if (std::strncmp(spec.start_room_id, map_.ActiveRoomId(),
                   MapRoomSpec::kIdLen) == 0) {
      const MapRoomSpec* sr = spec.FindRoom(spec.start_room_id);
      if (sr && sr->has_start_spawn) {
          room.player_start = sr->start_spawn;
          room.checkpoint   = sr->start_spawn;
      }
  }
  ```
  (Guard is explicit so a future non-start boot — continue-from-save, debug
  room — cannot teleport the player to Start.)
- Data shapes: uses existing `MapRoomSpec.start_spawn` / `has_start_spawn`
  (already populated by the bake + parsed by `LoadMapPack`).
- Integration points: `ResetPlayerToRoomStart()` is called by `BootMapPack`
  right after this override and reads `room.player_start` / `room.checkpoint`
  (existing), snapping via `snap_spawn_center` (floor verified under the
  Start spawn) and calling `camera_controller.Reset(camera, player.position)`
  (existing, `gameplay_scene.cpp:277`) — so boot repositions the player AND
  resets the camera in one path (per `camera-respawn-reset`).
- Error paths: `has_start_spawn=false` or a non-start boot → fall back to
  `.lvl` spawn (existing path); no crash.

#### Edge cases
- A start room that (after Inc 1) also contains other PlayerSpawns: the
  manifest field still wins (robust by construction).
- `snap_spawn_center` handles the 19-unit drop to the floor (existing probe
  logic); no change needed.

#### Verification
- Run: rebuild ROM (Inc 4), run under Ares; assert the boot log line
  `[spawn] authored=(0.00,25.60,89.60)` (emitted by `ResetPlayerToRoomStart`,
  `gameplay_scene.cpp:268`) and the player is grounded (no frame-1 transition
  to `cell_00_n01`, no falling velocity).
- Tests to add/update: extend `tests/map_runtime_smoke.cpp` to assert that
  the start room's `start_spawn` (when `has_start_spawn`) equals the world
  Start position and that `ResolveCellByPosition` on it returns
  `start_room_id`.
- Done: player boots standing on the floor in `cell_00_00`.

### Inc 3 — Harden colmesh reuse (opt-in) (S)
**Depends on:** 1
**Unblocks:** 4
**Done criteria:** `bake_map_pack.py` never silently reuses a colmesh; reuse
is explicit via `--reuse-colmesh`, and the hardcoded `triangles=944` stats hack
is gone.

**STATUS: DONE.** `write_chunk` gains `reuse_colmesh` (from `--reuse-colmesh`,
default off). On reuse, real vertex/triangle/BVH counts are parsed from the
CMSH header via new `read_colmesh_counts` (no hardcoded 944/511); an
unparseable reused file is unlinked and treated as no-colmesh. Without the
flag, a leftover `.colmesh` on a decoration-only chunk is explicitly unlinked
with a warning. `tests/map_pack_smoke.py` adds `test_colmesh_reuse_off_by_default`
(fresh bake never reuses; planted stale colmesh is removed on re-bake) and
`test_colmesh_reuse_opt_in` (REUSE log + real counts reported, file kept).
All 6 map_pack_smoke tests pass. (Test defaults/guards were also updated to
1200/1500/650 as part of this increment's verification — see Inc 4.)

#### Files to touch

##### tools/bake_map_pack.py
- What changes: `write_chunk` gains a `reuse_colmesh: bool` parameter (from a
  new `--reuse-colmesh` CLI flag, default `False`). The
  `elif colmesh_path.exists()` branch only runs when `reuse_colmesh` is true;
  otherwise a decoration-only chunk emits **no** colmesh — and any existing
  `.colmesh` left at that path by an earlier bake is **explicitly unlinked**
  (with a warning), so the DFS cannot ship stale collision from a previous
  bake into the same out-dir. When reuse is enabled, log loudly and compute
  the real triangle/node counts from the reused file instead of hardcoding
  944/511 (parse the CMSH header).
- Function(s):
  - `write_chunk(..., reuse_colmesh: bool = False) -> ChunkStats`
  - `main(argv)` — add `--reuse-colmesh` (store_true).
- Data shapes: unchanged (`ChunkStats`).
- Integration points: `bake_map_pack` calls `write_chunk` with the flag.
- Error paths: reuse requested but the existing colmesh has a different CMSH
  version or fails to parse → warn and treat as no-colmesh (do not crash).

#### Edge cases
- Re-baking over an existing out-dir with `reuse_colmesh=False` must leave no
  leftover `.colmesh` on decoration-only chunks (the unlink above).
- The earlier session's workflow (reuse to heal `cell_00_n01`) is preserved via
  the explicit flag.

#### Verification
- Run: `python3 tools/bake_map_pack.py ... --chunk-size 1200` (no flag) → no
  "using existing colmesh" lines; every solid chunk has a freshly-written
  `.colmesh`; decoration-only chunks have **no** `.colmesh` even if a previous
  bake left one.
- Tests to add/update: `tests/map_pack_smoke.py` — assert a fresh bake never
  reuses (no stale sha) and that re-baking over an existing bake leaves no
  leftover colmesh on decoration-only chunks; optional test with
  `--reuse-colmesh`.
- Done: bake is deterministic from input alone; no stale collision can leak.

### Inc 4 — Update tests/docs, rebuild ROM, emulator verification (M)
**Depends on:** 1, 2, 3
**Unblocks:** none
**Done criteria:** `./compile-rom.sh` produces a ROM where the player boots
standing on the floor in `cell_00_00` and can traverse across ≥ 2 chunk
boundaries without falling; all host smoke tests green; docs updated.

#### Files to touch

##### tests/map_pack_smoke.py
- What changes: default `chunk_size` 650 → 1200; the cap-guard test (currently
  `--chunk-size 1000`) becomes a face-cap guard with `--chunk-size 1500`
  (verified: `cell_n01_n02` exceeds 1024 faces) and a room-count guard with
  `--chunk-size 650` (verified: 118 > 64). Add an axis-regression assertion:
  for the bake's start room, `runtime ResolveCellByPosition(start_spawn)`
  equals the start room id — implemented host-side in the C++ smoke instead
  (below) since this is a Python test.
- Function(s): update fixtures/assertions.
- Data shapes: unchanged.

##### tests/mappack_smoke.py
- What changes: default `chunk_size=650` → `1200`.
- Function(s): fixture default only.

##### tests/mappack_loader_smoke.cpp
- What changes: the `chunk_size ∈ (649, 651)` assertion → `(1199, 1201)`.
- Function(s): assertion only.

##### tests/map_runtime_smoke.cpp
- What changes: default fixture path `/tmp/bake-fc-650` → `/tmp/bake-fc-1200`;
  **add Z-axis + seam regression cases** — (a) a position at world
  `z = −(chunk_size·scale + 1)` must resolve to the `-Z` neighbor of the start
  room (catches a re-introduction of the up-axis bug); (b) the
  boundary-equivalence case from Inc 1 (positions exactly on seams
  `world = (±k·cs·s, y, ∓k·cs·s)` for several `k`, bake `cell_of` == runtime
  `ResolveCellByPosition`); assert `start_spawn` matches the world Start
  position when `has_start_spawn`.
- Function(s): new test cases.
- Data shapes: unchanged.

##### tests/map_transition_smoke.cpp
- What changes: default fixture path → `/tmp/bake-fc-1200`; keep the existing
  transition assertions (they already pass against the corrected bake).
- Function(s): fixture path only.

##### docs/room_artifact_contract.md, docs/milestones.md, AGENTS.md
- What changes: document the world-XZ axis convention for grid chunking (map
  `(x, −y)` = world `(x, z)`), the `chunk_size=1200` default for `1.map`, the
  manifest `start_spawn` boot source, and the `--reuse-colmesh` opt-in.
  Update milestone status to reflect the fixup.
- Function(s): prose only.

##### .plans/whole-interconnected-map/plan.md
- What changes: append a short "FIXUP (see .plans/interconnected-map-fixup)"
  note under the Inc 1 STATUS pointing at this plan; reword the "60 chunks at
  `--chunk-size 650`" claim to "60 chunks under the old up-axis partition;
  the corrected world-XZ axis gives 118 cells at 650, so 1200 is used".
- Function(s): prose only.

##### Cleanup
- Confirm (Inc 1 already did) that stale `build/bake-fc-650/` and
  `filesystem/lvl/forsyken-city/*` were removed before the first re-bake; this
  increment re-verifies no old cell ids linger (per the stale-colmesh
  hazard).

#### Edge cases
- DFS size: 47 chunks at ~1200-unit cells are smaller per chunk than the 650
  bake; verify total DFS size stays under budget (it shrank).
- First make run after the change: `make bake-forsaken-city` re-bakes because
  the out dir changed; the DFS wildcard re-packs because the copied files are
  new. Force with `make clean` if make's timestamps are ambiguous.

#### Verification
- Run:
  ```sh
  rm -rf build/bake-fc-650 filesystem/lvl/forsyken-city
  python3 tests/map_pack_smoke.py
  python3 tests/mappack_smoke.py
  g++ -std=c++17 -Isrc/user tests/map_runtime_smoke.cpp \
    src/user/gameplay/world/map.cpp src/user/gameplay/world/level_loader.cpp \
    src/user/gameplay/world/mappack_loader.cpp \
    src/user/gameplay/physics/coll_mesh.cpp src/user/gameplay/physics/geom.cpp \
    -o /tmp/map_runtime_smoke && /tmp/map_runtime_smoke /tmp/bake-fc-1200/forsyken-city.mappack /tmp/bake-fc-1200
  # ... same for map_transition_smoke, mappack_loader_smoke, multichunk_save_smoke, cassette_transition_smoke
  ./compile-rom.sh
  ```
- Tests to add/update: as listed above.
- Manual: load `madeline_cube_rom.z64` in Ares → player boots standing on the
  floor; walk across ≥ 2 chunk boundaries (including a ±Z boundary, e.g. from
  `cell_00_00` south into `cell_00_n01`) without falling; collect a
  strawberry; die and respawn in the correct chunk.
- Done: whole A-side boots, is traversable across chunk boundaries, and no
  fall-through at seams.

**STATUS: DONE.** All tests updated to 1200 defaults; `map_runtime_smoke`,
`map_transition_smoke`, `mappack_loader_smoke`, `multichunk_save_smoke`,
`cassette_transition_smoke`, and Python `map_pack_smoke` / `mappack_smoke` /
`bake_parity_smoke` pass. `./compile-rom.sh` produces `madeline_cube_rom.z64`;
Ares boot shows `[spawn] authored=(0.00,25.60,89.60) grounded=(0.00,35.60,89.60)
grounded=1` and telemetry stays at `pos=(0.000,35.601,89.600)` with
`vel=(0.000,-10.000,0.000)` for the first 300 frames (no fall-through).

**Post-implementation fix (N64-only):** an additional `__attribute__((noinline))`
was applied to `RaycastRoomMesh` in `src/user/gameplay/world/world.cpp`. With
`-Os -ffast-math` on MIPS, the inlined return of `GroundHit{}` from
`RaycastRoomMesh` was being miscompiled/ABI-corrupted when called from the
boot snap path: the standalone `RaycastMesh` hit the floor, and the dot filter
did not reject it, yet `QueryFloorSource` reported `hit=0` and the player fell
through. Forcing the function out-of-line prevented the optimizer from
producing the bad inlined version. This is recorded as a load-bearing codegen
workaround, not behavior change.

## Cross-cutting verification

- After Inc 1: bake is 47 chunks, all ≤ caps, ≤ 64 rooms, containment holds,
  start room/spawn correct, and runtime `ResolveCellByPosition` agrees with the
  bake on both axes including exactly at seams (Start spawn → `cell_00_00`;
  `z < −240` → `cell_00_n01`; `x < −240` → `cell_n02_00`; seam points
  `world = (±k·240, y, ∓k·240)` agree). `filesystem/lvl/forsyken-city/` holds
  only new-axis chunks.
- After Inc 4: the ROM boots grounded at the Start spawn and traverses ≥ 2
  chunk boundaries (X and Z) without falling; the four shell probes pass
  against the active chunk (unchanged behavior, re-run).

## Standards / common-mistakes referenced

- `.agents/common-mistakes/missing-player-start-init.md` — applies to: Inc 2/4
  (boot is a spawn → reset is correct; chunk transitions still carry position,
  never reset — unchanged).
- `.agents/common-mistakes/dfs-path-prefix.md` — applies to: Inc 4 (chunk
  files must stay under `filesystem/lvl/forsyken-city/` and load as
  `rom:/lvl/forsyken-city/<chunk>.lvl` — unchanged).
- `.agents/common-mistakes/og-map-polygon-winding.md` — applies to: Inc 1
  (the re-bake reuses the existing writers unchanged; winding guards hold per
  chunk).
- `AGENTS.md` "Agent working rules" — inspect coordinate-system boundaries
  first when controls/movement feel wrong; this fix is exactly that boundary
  (bake partition vs runtime resolution).

## Open questions (CONSIDER from review)

- **Auto-tune chunk_size in the bake** (search the largest `--chunk-size`
  that fits caps and ≤ 64 rooms) instead of a fixed default. Nice-to-have; the
  fatal guards already make a fixed default safe. Revisit if other maps are
  added.
- **Per-room checkpoint spawns still resolve to `.lvl`'s last PlayerSpawn.**
  After Inc 1 this is correct for the start room, but a checkpoint room with
  multiple PlayerSpawns could snap a respawn to the wrong spawn. The save
  stores the checkpoint position separately; verify respawn in ROM and
  consider a per-room `checkpoint_spawn` manifest field if wrong.
- **`cell_of` world-space rewrite is now part of Inc 1** (adopted from review
  MUST-FIX 1 to eliminate FP seam divergence). Resolved — no longer open.
- **Boundary seam behavior in ROM.** The runtime's hysteresis keeps the active
  room on ties; if a real seam flip ever shows as a pop/hitch in the ROM,
  revisit (probe exactly-at-seam values are covered by the new smoke cases).
- **`world_aabb_for_cell` preload culling.** Now corrected to world space in
  Inc 1; the runtime's default ring is still 0 (active-only), so culling is
  exercised only when a ring > 0 is configured later.

## Out of scope

- Re-auditing the rest of the `whole-interconnected-map` increments (cassette
  Push/Pop, per-chunk save) beyond what the regression touches — they were not
  the cause and their smokes are green.
- T3DM renderer cutover, moving-platform `.nav` runtime, and the deferred
  actor behaviors — unchanged from the parent plan.
- A proper per-room checkpoint spawn manifest field (CONSIDER above).
