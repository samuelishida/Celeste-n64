# Faithful Conversion of Celeste 64 Level 1-1 to the N64 Demake Engine

## Context

The demake currently ships a hand-authored `first-room.map` (a tiny test level: spawn platform, gap, landing, climb wall, kill walkway, one Strawberry). The bake pipeline (`tools/bake_map.py` → `.lvl`, `tools/colmesh_bake.py` → `.colmesh`) is general enough to handle arbitrary Quake `.map` brushes, including non-axis-aligned slopes via plane-clipping. Render uses `LevelRenderer` (T3D packed-vertex batches per material), and collision uses a BVH'd `CollMesh`.

The original Celeste 64 ships `assets/og_converted/maps/1-1.map`: 48 brushes, 56 entities, ~270 brush-face plane definitions, six distinct textures, slopes/diagonal brushes, ten gameplay classes (`PlayerSpawn`, `Strawberry`, `Cassette`, `TrafficBlock`, `Node`, `SpikeBlock`, `DeathBlock`, `Decoration`, `StaticProp`, `func_group`).

This plan converts OG 1-1 into a playable, visually-faithful demake level. The shell, decorations, props, hazards, moving platforms, and end-of-level cassette all need to be honored.

## Architectural decisions

- **Decision:** Bake the OG `.map` directly with the existing pipeline. **Rationale:** the clipping algorithm already supports slopes; rewriting it for OG geometry is wasted work. **Alternative rejected:** intermediate JSON / Trenchbroom export — duplicates info already in the Quake format.
- **Decision:** Bump the on-disk level format to **LVL2** in Inc 4 and use the existing Face record's reserved field as `flags`. **Rationale:** Decoration/hazard/traffic visual policy needs an explicit face flag that every consumer can read consistently. **Alternative rejected:** keeping LVL1 and overloading `props_blob` for per-face semantics — that would make loader/baker/colmesh coupling more fragile, not less.
- **Decision:** `StaticProp` instances are baked into the entity table with a `model_str_id` string-table reference plus `angle`/`height`/`radius`, and rendered by a new `PropRenderer` that iterates the spawn list and draws one `T3DModel` per prop. **Rationale:** reuses the string table; PropRenderer is a thin loop. **Alternative rejected:** dedicated props blob with binary records — adds a format channel we don't need yet.
- **Decision:** Create a shared C++ entity-id header in Inc 2 (`src/user/gameplay/world/entity_ids.hpp`) and refactor the current hardcoded ids in `level_loader.cpp` and `entity_dispatch.cpp` onto it before adding new classes. **Rationale:** later increments need one extendable place for ids, and the repo does not have that today. **Alternative rejected:** continuing to sprinkle more magic numbers into loader/dispatch code.
- **Decision:** Per-brush "is solid" flag controls whether `colmesh_bake.py` emits triangles. Decoration brushes go to render only, never to CollMesh. **Rationale:** avoids inflating the BVH with non-colliding faces. **Alternative rejected:** separate render-only mesh file — extra DFS dep, extra loader.
- **Decision:** Hazard volumes (`SpikeBlock`, `DeathBlock`) ship as bake-time AABBs in the entity table and invoke an explicit `RespawnSystem::ForceRespawn(...)` hook on overlap. **Rationale:** kill check is cheap AABB-vs-player-sphere, and the current fall-height-only respawn path is not a general-purpose death API. **Alternative rejected:** overloading the fall-height check or coupling kill semantics to face materials.
- **Decision:** `TrafficBlock`/`Node` data stays **outside `ActorWorld`**. Loader populates compact `Room` arrays, `GameplayScene` owns the live moving-platform runtime state, and a dedicated `TrafficBlockRenderer` draws the platforms. **Rationale:** the current `ActorWorld` only holds borrowed pointers and has no solid/platform render path, while `Room::moving_surfaces` already provides the movement/carry data the motor consumes. **Alternative rejected:** routing platforms through `ActorWorld` before introducing owned storage, activation, and rendering hooks.
- **Decision:** Audio/atmosphere strings (`skybox`, `music`, `ambience`, `snowAmount`, `snowDirection`) ride through the loader into `Room`. Subsystems that consume them (audio, sky, snow) are stubs in this plan; only the plumbing lands. **Rationale:** keeps the bake/load contract honest while leaving real audio for a separate plan.

## Assumptions and answers from code

- **Map format**: OG 1-1 is Standard Quake `.map`. Source: `assets/og_converted/maps/1-1.map:1-7`. Confirmed by parsing.
- **Coordinate transform**: Quake → game = `(x * 0.2, z * 0.2, -y * 0.2)`. Source: `tools/bake_map.py:411`. Already applied uniformly.
- **Current bake skips brush-bearing non-shell classes**: Decoration, SpikeBlock, TrafficBlock, DeathBlock, func_group, Cassette get logged-and-dropped. Source: `tools/bake_map.py:27-40`. This plan changes that.
- **Current runtime entity ids are hardcoded** in `src/user/gameplay/world/level_loader.cpp` and `src/user/gameplay/world/entity_dispatch.cpp`; there is no shared C++ mirror yet. Inc 2 creates it before later increments add new ids.
- **MaterialCatalog reads from `.manifest` line-by-line**, with missing sprites skipped. Source: `src/user/gameplay/render/material_catalog.cpp:33-37`. Adding sprite files to DFS is sufficient for new textures.
- **Capacity constants** (verified): `LevelGeometry::kMaxFaces=512`, `LevelGeometry::kMaxVertices=4096` (`src/user/gameplay/world/level_loader.hpp:23-24`); `Room::kMaxSpawns=16` (`src/user/gameplay/world/world.hpp:118`); `LevelRenderer::kMaxBatches=512` and `LevelRenderer::kMaxVerts=4096` (`src/user/gameplay/render/level_renderer.hpp:26-27`). **Note:** `LevelRenderer::kMaxVerts` is declared but *unused* — the verts buffer is dynamically allocated from `geometry.vertex_count` (`level_renderer.cpp:37`). No renderer-side vertex cap needs bumping; only `LevelGeometry::kMaxVertices` matters.
- **OG 1-1 bounding box** (verified by re-parsing 1-1.map): excluding the DeathBlock kill-volume marker (Quake y up to 4864), the playable shell spans:
  - Quake x ∈ [-960, 384] → game x ∈ [-192.0, 76.8]
  - Quake z ∈ [240, 576] → game y ∈ [48.0, 115.2]
  - Quake y ∈ [-138, 4544] → game z ∈ [-908.8, 27.6]
  - Max absolute coord = 908.8 game units.
- **kPosFp choice**: `src/user/gameplay/render/level_renderer.cpp:20`. At kPosFp=128 the int16 range is ±256; at 64 it is ±512; at **32** it is ±1024. OG 1-1 reaches 908.8 game units (forward axis through the level). Plan lowers kPosFp to **32** in Inc 2 (32*908.8 = 29082 < 32767, comfortable margin).
- **DeathBlock kill-volume Quake-y up to 4864** (game-z up to -972.8) sits outside the playable shell. This is handled in Inc 7 by emitting DeathBlock with **no faces** (only an AABB entity record), so its giant extent never enters the vertex packing path.
- **StaticProp model files** present at `assets/og_converted/models/{tree1,bush1,grass1,hydrant}.t3dm`. Source: `assets/og_converted/models/`. Need DFS copy step.
- **OG textures** present at `assets/og_converted/textures/{snow_1,rock_2,metal_floor_1,floor_dirty_concrete,TB_empty}.sprite`. Source: `assets/og_converted/textures/`. Need DFS copy step.
- **User chose Recommended on all four scope questions**: full faithful, all textures, bake props, stub audio. Source: user-confirmed.
- **TB_empty texture is the "invisible" marker** (kill volumes use it for OG visual hiding). Source: `assets/og_converted/textures/TB_empty.sprite` exists; OG uses on DeathBlock faces. Renderer treats it as a no-draw material id.
- **func_group entities are TrenchBroom layer markers** (no runtime semantics). Source: `assets/og_converted/maps/1-1.map:835-841` ("_tb_type" "_tb_layer"). Baker skips them.

## Risks accepted

- **`LevelGeometry::kMaxFaces=512` may not fit decorations.** The current shell-only 1-1 bake is 102 faces / 440 vertices / 2 entities. Decoration brushes are added later and may push the level beyond the old first-room-oriented caps, so Inc 2 bumps `LevelGeometry::kMaxFaces` to 1024 unconditionally and `LevelRenderer::kMaxBatches` to 1024 to match (one batch per face today).
- **Static struct growth on the heap.** Doubling `LevelGeometry::kMaxFaces` adds ~25 KB; doubling `LevelGeometry::kMaxVertices` adds ~80 KB; bumping `LevelRenderer::kMaxBatches` to 1024 adds ~6 KB. Total ~110 KB inside `GameplayScene::Impl` (heap-allocated). N64 RDRAM is 4–8 MB; acceptable but tracked.
- **PropRenderer model loads may be slow.** Each `t3d_model_load` is a DFS read; 11 props means 11 file opens. Mitigation: dedupe by model name and load each `.t3dm` once at level init.
- **`Node` targets can be unresolved** (missing targetname). Mitigation: baker logs and emits TrafficBlock with empty node list; actor stays still rather than crashing.
- **Slope collision may interact poorly with player motor.** The motor was tuned on axis-aligned faces of `first-room`. Mitigation: Inc 2 includes a "walk the level on a stick" smoke check; if the player falls off slopes or warps, file a follow-up bug. Don't block plan completion on it.
- **DFS size growth.** Adding 5 textures + 4 models adds ~150 KB. Mitigation: accept; N64 cart has room.
- **Skybox/music are stubbed.** Atmospheric mismatch (silent, no sky). Mitigation: accept; tracked in Open Questions.

## Increment DAG

```
Inc 1 (S) ─→ Inc 2 (M) ─┬─→ Inc 3 (S, loader plumbing) ─┬─→ Inc 5 (L)
                        │                                └─→ Inc 9 (M)
                        └─→ Inc 4 (M, LVL2 format bump) ─┬─→ Inc 6 (M) ─→ Inc 7 (S)
                                                         └─→ Inc 8 (L)

Inc 6 and Inc 8 also require Inc 3's shared string-table / props-blob reader.
```

- Inc 1 (S) — Texture pack expansion → unblocks 2
- Inc 2 (M) — Bake 1-1 shell + capacity bumps + load swap → unblocks 3, 4
- Inc 2 also creates `src/user/gameplay/world/entity_ids.hpp`, rewires the existing loader/dispatch callsites to use it, and normalizes `ActorWorld::ResolvePending()` in scene init/reload. Later increments extend that header instead of adding more local constants.
- Inc 3 (S) — Loader string table + props blob + atmos/audio pipe-through → unblocks 5, 6, 8, 9
- Inc 4 (M) — Decoration brushes + **LVL format bump to LVL2** → unblocks 6, 8
- Inc 5 (L) — StaticProp baking + PropRenderer → terminal
- Inc 6 (M) — SpikeBlock hazard volumes → unblocks 7
- Inc 7 (S) — DeathBlock kill volume → terminal
- Inc 8 (L) — TrafficBlock + Node path-follow on `Room::moving_surfaces` + dedicated renderer → terminal
- Inc 9 (M) — Cassette + level-end transition → terminal

**Critical ordering**: Inc 3 introduces reusable string-table and `props_blob` loading. Inc 4 introduces the `flags` field on Face records (LVL1→LVL2 format bump). Inc 6 and Inc 8 need both foundations. Inc 7 must land after Inc 6.

Increments 3 and 4 can run in parallel after Inc 2. Increments 5 and 9 can run after Inc 3. Increments 6 and 8 can run after both Inc 3 and Inc 4. The shared entity-id files (`tools/entity_ids.py` and the new `src/user/gameplay/world/entity_ids.hpp`) get one line per Inc 5/6/7/8/9; if these run concurrently in branches, merge conflicts are inevitable — coordinate via sequential merges, not parallel commits to the same file.

---

## Increments

### Inc 1 — Texture pack expansion (S) [done]

**Depends on:** none
**Unblocks:** 2
**Done criteria:** Building the ROM with a manifest listing all six OG textures reserves a `MaterialCatalog` slot for each (including a null slot for `TB_empty`). `[matcat] material[i]=NAME OK` debug lines confirm each non-empty load. Material indices are stable across baker and runtime.

#### Files to touch

##### `Makefile`
- What changes: Add five `.sprite` files to `DFS_TEX_FILES`.
- Lines: extend the variable to include `snow_1`, `rock_2`, `metal_floor_1`, `floor_dirty_concrete`, `TB_empty`. `rock_1` and `rock_1_climbable` stay.
- Integration: pre-existing pattern rule `filesystem/tex/%.sprite: assets/og_converted/textures/%.sprite` handles them.

##### `src/user/gameplay/render/material_catalog.hpp`
- What changes: no field changes (kMaxMaterials=32 stays). Add a comment documenting that `sprites_[i] == nullptr` is a valid slot meaning "invisible material" (used by `TB_empty`).

##### `src/user/gameplay/render/material_catalog.cpp`
- What changes: in `Load`, **stop early-`continue`-ing when a sprite is missing or named `TB_empty`** — instead reserve the slot with `sprites_[count_] = nullptr; ++count_;`. This keeps material-id ordering stable between baker and runtime regardless of which files exist in DFS.
  - The current behavior (line 33–37 + 49–51): skip the slot entirely on missing file, which silently shifts subsequent material indices. Replace with the slot-reserving variant.
- Add explicit `TB_empty` handling: even if the sprite file *does* exist in DFS (which it will after the Makefile change), insert a null slot for it. Simplest: name-check `strcmp(line, "TB_empty") == 0` and skip the `sprite_load` call, going straight to null insertion.

##### `src/user/gameplay/render/level_renderer.cpp`
- What changes: in `Draw`, when `material == nullptr`, `continue` past the batch (skip upload + tri draws). The current code already early-returns on the upload path (line 136 `if (... && material != nullptr)`) but still issues the tri draws — change to a hard `continue` at the top of the batch loop when material is null.

#### Atomicity note
Baker, MaterialCatalog, and renderer must agree on the meaning of "null slot." Baker writes material names verbatim into the manifest (including `TB_empty`); MaterialCatalog reserves a null slot for it; renderer skips batches whose material slot is null. All three changes must land together in this increment.

#### Edge cases
- A texture listed in `.manifest` but absent from DFS — already handled (logged-and-skipped).
- The order materials appear in the manifest determines their numeric id. Baker writes them in order of first use, which is deterministic.

#### Verification
- Run: `./compile-rom.sh` then boot ROM and check `[matcat] loaded N materials` line in console.
- Done: N ≥ 6 for `1-1.manifest` (only meaningful after Inc 2 swaps the level).
- Self-check for Inc 1 alone: build still passes; sprites copied to `filesystem/tex/`.

---

### Inc 2 — Bake 1-1 shell + capacity bumps + load swap (M) [done]

**Depends on:** Inc 1
**Unblocks:** 3, 4
**Done criteria:** ROM boots into 1-1, world shell renders with full texture set, player spawns at OG-defined coords, can walk on the spawn platform without falling through.

#### Files to touch

##### `Makefile`
- What changes: Add `1-1` level to `DFS_LVL_FILES`. Add bake rule.
- Add:
  ```makefile
  DFS_LVL_FILES += \
      filesystem/lvl/1-1.lvl \
      filesystem/lvl/1-1.manifest \
      filesystem/lvl/1-1.colmesh

  filesystem/lvl/1-1.lvl filesystem/lvl/1-1.manifest: \
      assets/og_converted/maps/1-1.map | filesystem/lvl
  	python3 tools/bake_map.py $< filesystem/lvl/1-1.lvl filesystem/lvl/1-1.manifest
  ```
- The existing `filesystem/lvl/%.colmesh: filesystem/lvl/%.lvl` rule picks up `1-1.colmesh` automatically.
- Optional: keep `first-room.*` rules in place so the test room can be selected by changing one line in `gameplay_scene.cpp` (do not remove it).

##### `src/user/gameplay/world/level_loader.hpp`
- What changes: `LevelGeometry::kMaxFaces` 512 → 1024, `LevelGeometry::kMaxVertices` 4096 → 8192.
- Rationale: shell-only 1-1 currently bakes to 102 faces / 440 vertices / 2 entities, and later visual-only Decoration brushes plus hazards will grow that. The cap bump lands early so the ROM path has headroom before later content increments.

##### `src/user/gameplay/world/world.hpp`
- What changes: `Room::kMaxSpawns` 16 → 64.
- Rationale: OG 1-1 has ~31 spawnable entities (PlayerSpawn, Strawberry, Cassette, TrafficBlocks, Nodes, SpikeBlocks, DeathBlock, StaticProps). 64 leaves headroom.

##### `src/user/gameplay/world/entity_ids.hpp` (NEW)
- What changes: create the shared C++ mirror for the existing baked-entity ids:
  ```cpp
  namespace madeline_cube {
  static constexpr uint16_t kEntPlayerSpawn = 0;
  static constexpr uint16_t kEntStrawberry  = 1;
  static constexpr uint16_t kEntRefill      = 2;
  static constexpr uint16_t kEntSpring      = 3;
  }  // namespace madeline_cube
  ```
- Later increments extend this same file with `StaticProp`, `SpikeBlock`, `DeathBlock`, `TrafficBlock`, `Node`, and `Cassette`.

##### `src/user/gameplay/world/level_loader.cpp`
- What changes: remove the local `kEntPlayerSpawn` constant, include the new `entity_ids.hpp`, and use the shared names for the existing switch/if branches.

##### `src/user/gameplay/world/entity_dispatch.cpp`
- What changes: include `entity_ids.hpp` and replace the raw `1/2/3` classname ids in `ClassnameToPlaceholder(...)` with the shared constants.

##### `src/user/gameplay/render/level_renderer.hpp`
- What changes: `LevelRenderer::kMaxBatches` 512 → 1024 (one face per batch, matching the bumped `LevelGeometry::kMaxFaces`).
- `LevelRenderer::kMaxVerts` is unused (the vert buffer is dynamically allocated from `geometry.vertex_count` at `level_renderer.cpp:37`); leave it alone, but add a `// unused — see Init()` comment for future readers. Per-material batch grouping is **out of scope for this plan** and tracked in "Out of scope."

##### `src/user/gameplay/render/level_renderer.cpp` (kPosFp)
- What changes: lower `kPosFp` from 128 to **32**.
- Rationale: OG 1-1 playable shell spans up to 908.8 game units in the game-z axis (Quake-y forward extent). `908.8 * 32 = 29082 < 32767` fits with margin. `kPosFp=64` would overflow (`908.8 * 64 = 58163`). Update the comment block to spell out the per-axis bounds and the new ±1024-game-unit ceiling.
- Identity matrix scale `1/kPosFp` (line 102) updates automatically since it derives from the constant.

##### `src/user/gameplay/scene/gameplay_scene.cpp`
- What changes:
  - Before editing, inspect the existing local diff around `kFirstRoomStaticModelPath` and `static_room_model`; preserve unrelated user edits while removing the obsolete first-room-only static room path.
  - Line 213: delete the `impl_->static_room_model.Load(kFirstRoomStaticModelPath)` path instead of gating it. Since this plan does not provide a `1-1.t3dm`, the baked `LevelRenderer` becomes the single world-geometry render path.
  - Line 216: `LoadLevel("rom:/lvl/1-1.lvl", impl_->room, impl_->level_geometry)`.
  - Line 219: `impl_->material_catalog.Load("1-1")`.
  - Immediately after `DispatchLevelEntities(...)`, call `impl_->actor_world.ResolvePending();` so baked collectible actors are active on the same session init path that later reloads will use.
  - Line 441–443: drop the `else if (impl_->static_room_model.IsLoaded())` branch.
  - Line 265: drop `impl_->static_room_model.Free()`.
  - Field on `Impl`: remove `StaticRoomModel static_room_model;` member.
- Rationale: the static T3DM was a parallel render path for `first-room` only; with 1-1 baked, the level renderer is the only source of world geometry.

##### `tools/bake_map.py`
- What changes: extend the texture-output debug line to print all distinct material names found, ordered.
- Verify the existing path handles slopes/decimal coords. No code change unless a parse bug surfaces.

##### `tests/level_loader_test.cpp` and `tests/level_bake_report_smoke.py`
- What changes:
  - Keep the existing `first-room` smoke test as a stable regression: build pipeline must still bake `first-room.lvl` correctly (the Makefile rule is preserved alongside the new `1-1` rule).
  - Add a parallel shell-only 1-1 case: bake `1-1.map`, assert `face_count >= 100`, `vertex_count >= 400`, `entity_count == 2`, and materials include `rock_1`, `rock_2`, and `snow_1`.
  - Later increments own higher count assertions: Inc 4 should assert Decoration increases `face_count`, Inc 5 should assert StaticProp records, Inc 6/7 should assert hazard counts, Inc 8 should assert traffic/node counts, and Inc 9 should assert cassette presence.
  - Do **not** delete the first-room fixture lines — the test verifies the baker on a tiny, controllable shape.

#### Edge cases
- Slope colmesh: re-bake produces non-axis-aligned triangles. Run `tools/colmesh_dump.py filesystem/lvl/1-1.colmesh | head -40` to spot-check normals after bake.
- A brush face that clips to <3 vertices is already dropped (`bake_map.py:519`).
- kMaxFaces/kMaxVertices overflow is checked in loader and logs `exceeds kMax*`; if it triggers in Inc 2, bump again.

#### Verification
- Run: `./compile-rom.sh > /tmp/hawk-implement-plan-check-inc2.log 2>&1` then `rg -n 'error|warning|fail|FAIL' /tmp/hawk-implement-plan-check-inc2.log | head -50`.
- Boot ROM, observe console: `[lvl] loaded rom:/lvl/1-1.lvl: faces=N vertices=M entities=K`. Confirm N ≤ 1024, M ≤ 8192, K ≤ 64.
- Walk the spawn platform: player should not fall through, walls should push back. Slope walk is a stretch goal (do not block on perfect slope feel).
- Capture before/after vertex/face counts; document in commit body.

---

### Inc 3 — Loader string table, props blob, and atmos pipe-through (S) [done]

**Depends on:** Inc 2
**Unblocks:** 5, 6, 8, 9
**Done criteria:** Loader reads the LVL string table and `props_blob` into temporary load-time storage, exposes safe lookup/decode helpers for later entity increments, and stores skybox/music/ambience/snow fields on `Room`. Debug log prints the atmosphere values once at level init. No audio/snow rendering — strings ride to a subsystem boundary that doesn't yet consume them.

#### Format-supports-it note

The LVL header **already carries** `skybox_str_id`, `music_str_id`, `ambience_str_id`, `snow_amount_q8`, `snow_dir`, and `off_props_blob` (see `tools/lvl_format.py:11-24`). `bake_map.py:447-466` already populates the atmosphere fields from worldspawn keys. The loader currently *reads-and-discards* atmosphere fields, ignores `off_strings`, and ignores `off_props_blob`. This increment adds the shared **read + lookup/decode** path on the C++ side; baker and format are untouched.

After Inc 4's LVL2 bump, the same fields stay at the same header offsets — the format bump only affects Face records.

#### Files to touch

##### `src/user/gameplay/world/world.hpp` (Room struct)
- What changes: Add fields to `Room`:
  ```cpp
  char skybox[16];      // null-terminated string from manifest
  char music[24];
  char ambience[16];
  float snow_amount;
  Vec3  snow_dir;
  ```
- Sizes pulled from longest OG string + slack.

##### `src/user/gameplay/world/level_loader.cpp`
- What changes: replace the discard-block (`ReadU16(f); ReadU16(f); ReadU16(f);`) for skybox/music/ambience string IDs. Read them, then look up in the string table (which currently `level_loader` skips — restore the read).
- The string table is at `off_strings`. Read it before reading entities. Indexed by uint16 string ids in the header.
- Read `off_props_blob` instead of discarding it. Load the trailing `props_blob` region into a temporary heap buffer sized from file length minus `off_props_blob`; free it before returning from `LoadLevel`.
- Add small local helpers in `level_loader.cpp` for later increments to reuse:
  ```cpp
  const char* StringAt(uint16_t id);
  bool PropsRange(uint32_t offset, uint32_t len, const uint8_t** out);
  ```
  These helpers live inside `LoadLevel`/local loader context, not as public API.
- Copy looked-up strings into the fixed-size buffers on `Room`. If absent (str_id out of range, or `string_count == 0`), leave empty.
- Read snow_amount_q8 and convert to float (`q8/256.0f`). Read snow_dir int16 triple and convert to `Vec3` floats (`/256.0f`).
- Log once: `debugf("[lvl] atmos: sky=%s music=%s amb=%s snow=%.2f dir=(%.2f,%.2f,%.2f)\n", ...)`.
- Keep entity behavior unchanged in this increment: unknown/new entity ids still fall through to `room.spawns[]` until their owning increments add dedicated decode branches.

#### Edge cases
- Until Inc 4 lands, old `.lvl` files (including `first-room`) are still LVL1 format with the same string table and should keep working. Inc 4 intentionally makes LVL2 mandatory and re-bakes first-room.
- String id of 0 — valid index; not a sentinel. Use `string_count == 0` to mean "no strings".
- Empty props blob — valid; `PropsRange(...)` should return false unless `len == 0`.

#### Verification
- Run: `./compile-rom.sh > /tmp/hawk-implement-plan-check-inc3.log 2>&1`, rg as before.
- Boot ROM: expect `[lvl] atmos: sky=bsides music=mus_lvl1_bside amb=mountain snow=0.50 dir=(0.00,1.00,0.00)` printed once (the OG `snowDirection` is transformed from Quake space into game space during bake).

---

### Inc 4 — Decoration brushes + face flags (LVL format bump) (M) [done]

**Depends on:** Inc 2
**Unblocks:** 6, 8
**Done criteria:** OG 1-1 renders the 23 Decoration brushes (small snow lumps along the path). They have no collision: a player sphere passes through them.

#### Format bump

This increment introduces the `flags` field on Face records. The change is **on-disk binary breaking** because the field repurposes the existing `reserved int16` slot.

**Strategy:** bump the LVL magic to `LVL2` and the version uint32 to `2`. All four consumers — `tools/lvl_format.py` (read+write), `tools/bake_map.py` (always writes new format), `tools/colmesh_bake.py` (reads), and `src/user/gameplay/world/level_loader.cpp` (reads) — change atomically in this increment.

- LVL1 files become unreadable. Re-bake all levels from `.map` source as part of the bake step (the Makefile rules already invalidate `.lvl` on `.map` change; force a clean rebuild via `rm -f filesystem/lvl/*.lvl filesystem/lvl/*.colmesh` before re-building).
- No version-tolerant loader. The loader hard-errors on `LVL1` and tells the user to re-bake.
- Re-bake `first-room` as LVL2 in the same build pass and update all first-room fixture/test expectations that inspect magic/version.

#### Files to touch

##### `tools/lvl_format.py`
- What changes:
  - Magic: `b"LVL1"` → `b"LVL2"` in both `_write_binary` and `_read_binary`.
  - Version: `1` → `2`.
  - Face encoding format: `">IIHh"` → `">IIHH"`. Update both write (line 136) and read (line 217).
  - Face class: rename the trailing `_` reserved int16 to `flags: uint16` (default 0).
  - Update the docstring at the top of the file to document the new flags bit layout:
    ```
    Face.flags (uint16):
      bit 0 = solid           // contributes to CollMesh BVH
      bit 1 = visual_only     // render only; no collide
      bits 2-15 = reserved (must be 0)
    ```

##### `tools/bake_map.py`
- What changes:
  - Remove `"Decoration"` from `UNSUPPORTED_BRUSH_CLASSES`; add a new `VISUAL_ONLY_BRUSH_CLASSES = {"Decoration"}` set.
  - When emitting Face records, set `flags = 0x01` (solid) for worldspawn/func_wall/func_climbable faces, and `flags = 0x02` (visual_only) for Decoration faces.
  - For VISUAL_ONLY brushes: emit faces/vertices like worldspawn but **do not** append `lvl.colliders` entries.
  - Sort the face list so visual-only faces come *after* solid faces, preserving per-segment material grouping (keeps T3D batching efficient).

##### `src/user/gameplay/world/level_loader.cpp`
- What changes:
  - Magic check: accept `"LVL2"`, hard-error on `"LVL1"` with a debug message: `[lvl] file is LVL1; re-bake required (run 'make clean && make')`. Update line 68.
  - Version check: accept `2`, hard-error on `1` with the same re-bake message.
  - Face read (line 117–118): replace `ReadU16(f); ReadS16(f);` for `material_id`/reserved with `ReadU16(f); ReadU16(f);` reading material_id and flags. Store flags on `LevelFace`.

##### `src/user/gameplay/world/level_loader.hpp`
- What changes: add `uint16_t flags;` to `LevelFace`. Document the same bit layout as the Python side.

##### `tools/colmesh_bake.py`
- What changes:
  - Magic/version: read `LVL2` (or rely on `LvlFile.read` which checks).
  - When iterating faces, skip any face with `flags & 0x02` (visual_only). Decoration faces never reach the BVH.
  - Console summary: `[colmesh] solid_faces=N visual_only_skipped=M`.

##### `src/user/gameplay/render/level_renderer.cpp`
- What changes: no functional change. Renders all faces; visual-only faces are real geometry and should appear in the scene. The TB_empty material handling from Inc 1 already covers invisible kill-volume faces.

##### `tests/level_bake_report_smoke.py` and `tests/level_loader_test.cpp`
- What changes:
  - Update expected magic/version values to `LVL2`/`2`.
  - Update count assertions to account for new visual-only faces appearing in `face_count` but **not** in `collider_count`.

#### Ordering note for Inc 6, 7, 8
Inc 6 (SpikeBlock) and Inc 8 (TrafficBlock) require both Inc 3's string-table/`props_blob` helpers and Inc 4's Face `flags` field. Inc 7 (DeathBlock) depends on Inc 6. Do not implement any of them until those foundations have landed in that order.

#### Edge cases
- A Decoration brush whose AABB overlaps a solid brush — fine; render z-fight risk acceptable (decoration tris are small enough that they sit outside the shell).
- Decoration with `TB_empty` material — visual-only **and** invisible; baker emits faces but renderer skips them, free of cost.

#### Verification
- Run bake locally first: `python3 tools/bake_map.py assets/og_converted/maps/1-1.map filesystem/lvl/1-1.lvl filesystem/lvl/1-1.manifest`. Check `face_count` grows above the Inc 2 shell-only baseline while `collider_count` stays at the solid-face count.
- ROM boot: visually confirm snow lumps appear on platform edges.
- Walk into a decoration: player should pass through (no collision response).

---

### Inc 5 — StaticProp baking + PropRenderer (L)

**Depends on:** Inc 3
**Unblocks:** none
**Done criteria:** Trees/bushes/grass/hydrant from OG 1-1 appear at their authored positions with correct rotation. No collision (props are visual). Drawing 11 props doesn't tank framerate below 25 fps.

#### Files to touch

##### `Makefile`
- What changes: Add `tree1.t3dm`, `bush1.t3dm`, `grass1.t3dm`, `hydrant.t3dm` to `DFS_MDL_FILES`.
- Pre-existing pattern rule `filesystem/mdl/%.t3dm: assets/og_converted/models/%.t3dm` handles the copy.

##### `tools/entity_ids.py`
- What changes: Add `"StaticProp": 4` to `ENTITY_IDS`.

##### `src/user/gameplay/world/entity_ids.hpp` (extend shared header from Inc 2)
- What changes: add `kEntStaticProp = 4`.

##### `tools/bake_map.py`
- What changes:
  - Point-entity branch: when `classname == "StaticProp"`, intern `entity["model"]`, parse `angle`/`height`/`radius`.
  - Encode the extras into the `Entity.props_offset/props_len` channel (`props_blob` already exists in `LvlFile`). Format per prop:
    ```
    uint16 model_str_id
    int16  angle_deg_q8       // angle is whole degrees in OG; q8 keeps room for fractional
    int16  height_q8          // game-units * 256
    int16  radius_q8
    uint16 reserved
    ```
  - Append per-prop record to `lvl.props_blob`; set `Entity.props_offset` to byte offset, `props_len = 12`.
- Update `lvl_format.py` Entity to encode props_offset/props_len in the existing layout (no schema change — already in the format).

##### `src/user/gameplay/world/world.hpp` (Room)
- What changes: add fields:
  ```cpp
  static constexpr int kMaxProps = 16;
  struct PropSpawn {
      Vec3 position;
      char model_name[16];   // null-terminated; resolved from string table at load
      float angle_rad;
      float height;          // game units; stored but unused in this plan
      float radius;          // game units; stored but unused
  };
  PropSpawn props[kMaxProps];
  int prop_count = 0;
  ```
- Total per-instance size ~36 bytes; 16 instances ~576 bytes added to `Room`.

##### `src/user/gameplay/world/level_loader.cpp`
- What changes: when reading entities, on `classname_id == kEntStaticProp`:
  - Decode the `props_blob` slice (`Entity.props_offset .. props_offset + props_len`) into `model_str_id`, `angle_deg_q8`, `height_q8`, `radius_q8`.
  - Look up `model_str_id` in the string table (already read in Inc 3) and copy into `PropSpawn::model_name`.
  - Convert q8 fields to floats (`/256.0f`); convert `angle_deg` to radians.
  - Append to `room.props[]` if `prop_count < kMaxProps`.
- Use the string-table and `PropsRange(...)` helpers introduced in Inc 3; do not add a second independent `props_blob` reader here.

##### `src/user/gameplay/render/prop_renderer.hpp` (NEW)
- Class `PropRenderer` with:
  ```cpp
  class PropRenderer {
   public:
      bool Init(const Room& room);
      void Free();
      void Draw() const;

   private:
      static constexpr int kMaxModels = 8;     // distinct .t3dm files
      static constexpr int kMaxInstances = Room::kMaxProps;

      struct LoadedModel {
          char name[16];      // stem matched against PropSpawn::model_name
          T3DModel* model;
      };
      LoadedModel models_[kMaxModels];
      int model_count_ = 0;

      struct Instance {
          int model_index;    // index into models_
          T3DMat4FP* matrix_fp;
      };
      Instance instances_[kMaxInstances];
      int instance_count_ = 0;

      int FindOrLoadModel(const char* model_name);  // linear scan, then load
  };
  ```
- Memory model:
  - Each `T3DModel*` lives in main RDRAM (libdragon `t3d_model_load` allocs). Freed via `t3d_model_free` in `Free`.
  - Each `T3DMat4FP*` is 64 bytes uncached RDRAM (via `malloc_uncached`); freed via `free_uncached` in `Free`.
  - Worst case: 8 models + 16 matrices = ~50 KB (models) + 1 KB (matrices). Acceptable.
- Model dedup: linear scan over `models_[]` (8 entries max) by `model_name` strcmp. No hash map needed.

##### `src/user/gameplay/render/prop_renderer.cpp` (NEW)
- `Init` walks `room.props`:
  - Strip the `Models/` prefix and `.glb` suffix from `PropSpawn::model_name` to get the stem (e.g. `Models/tree1.glb` → `tree1`).
  - `FindOrLoadModel(stem)`: if found in `models_[]`, reuse; else `t3d_model_load("rom:/mdl/<stem>.t3dm")`, store, return index.
  - Allocate a per-instance `T3DMat4FP*` via `malloc_uncached`; bake the matrix with `t3d_mat4_from_srt_euler` using rotation `(0, angle_rad, 0)`, scale `(1, 1, 1)`, position `prop.position`.
  - Skip props whose model fails to load (warn `[prop] model %s missing`).
- `Draw` iterates `instances_[]`: `t3d_matrix_push(matrix_fp); t3d_model_draw(model); t3d_matrix_pop(1)`.
- `Free` walks instances and frees matrices; walks `models_[]` and `t3d_model_free`s each.

##### `src/user/gameplay/scene/gameplay_scene.cpp`
- What changes:
  - Add `PropRenderer prop_renderer;` to `Impl`.
  - Construct/init after `LoadLevel` (initialized from `room.props`).
  - Call `prop_renderer.Draw()` between `level_renderer.Draw(...)` and actor draws.
  - Call `prop_renderer.Free()` in scene teardown, before `level_renderer.Free()`.

#### Edge cases
- A prop whose model file is missing in DFS — load fails, log, skip instance.
- Angle in OG is degrees (often 180). Apply `angle_rad = angle_deg * π/180`. The "facing direction" maps to a Y-axis rotation in game space.
- `height`/`radius` ignored in this increment (props are visual-only — no collider).
- Prop position is the brush origin in OG units. Pass through `transform_point`.

#### Verification
- Bake: confirm `entity_count` includes 11 props; `props_blob` non-empty.
- ROM boot: trees/bushes appear at their authored positions. Spot-check tree heights look sensible (no shrunken stubs, no skyscrapers).
- Profile: framerate stays ≥ 25 fps with props on-screen. If not, defer grass1 instances to a follow-up plan (the dropdown choice the user already considered).

---

### Inc 6 — SpikeBlock hazard volume (M)

**Depends on:** Inc 3 and Inc 4 (requires `props_blob` helpers and Face `flags` field)
**Unblocks:** Inc 7
**Done criteria:** Touching a SpikeBlock force-respawns the player through an explicit `RespawnSystem` hook, and the existing same-frame camera-reset path still holds. SpikeBlock brushes render with `floor_dirty_concrete` (the OG material). Six SpikeBlock instances appear in OG 1-1.

#### Files to touch

##### `tools/entity_ids.py`
- Add `"SpikeBlock": 5`.

##### `src/user/gameplay/world/entity_ids.hpp` (extend shared header from Inc 2)
- Add `kEntSpikeBlock = 5`.

##### `tools/bake_map.py`
- What changes:
  - Move `"SpikeBlock"` out of `UNSUPPORTED_BRUSH_CLASSES`.
  - When emitting a SpikeBlock:
    - Bake its brush faces with `FLAG_VISUAL_ONLY` set (no collide via static colmesh — its kill volume is the AABB, not the BVH).
    - Compute the brush AABB and emit a `SpikeBlock` entity record at the AABB center. Store the half-extents in the `props_blob` channel:
      ```
      float half_extents[3]   // game-units
      uint16 reserved
      uint16 reserved
      ```
    - `props_len = 16`.

##### `src/user/gameplay/world/world.hpp` (Room)
- Add fields:
  ```cpp
  static constexpr int kMaxHazards = 16;   // OG 1-1 has 6 spikes + 1 death = 7
  struct HazardVolume {
      Vec3 center;
      Vec3 half_extents;
      uint8_t kind;     // 0=spike, 1=death  (Inc 7 introduces kind=1)
      uint8_t pad[3];
  };
  HazardVolume hazards[kMaxHazards];
  int hazard_count = 0;
  ```

##### `src/user/gameplay/world/level_loader.cpp`
- When `classname_id == kEntSpikeBlock`:
  - Decode `props_blob` slice via the Inc 3 `PropsRange(...)` helper (16 bytes: `float[3] half_extents`, 4 bytes reserved).
  - Append `HazardVolume{center=entity.position, half_extents, kind=0}` to `room.hazards`.

##### `src/user/gameplay/actor/spike_actor.cpp` (NEW; header exists at `spike_actor.hpp`)
- Header (modify existing): declare a free function inside the file:
  ```cpp
  bool CheckHazardKill(const Room& room, const Vec3& player_pos, float player_radius);
  ```
- `.cpp` body: iterate `room.hazards`. For each, treat as sphere-vs-AABB: clamp `player_pos` to `[center - half_extents, center + half_extents]`, compute squared distance to player; overlap if `dist_sq < player_radius * player_radius`. Return true on first overlap.

##### `src/user/gameplay/world/respawn_system.hpp` and `.cpp`
- What changes: add an explicit full-scene death hook:
  ```cpp
  void ForceRespawn(
      PlayerState& player,
      const Vec3& checkpoint,
      const Room& room,
      const PlayerMotor& motor
  ) const;
  ```
- Implementation: reuse the existing `ResetState(...)` helper and the same `motor.RefreshContacts(...)` path the fall-height respawn already uses. `Step(...)` remains the fall-threshold probe; `ForceRespawn(...)` is the unconditional version hazards and future cassette/end-state logic can call directly.

##### `src/user/gameplay/world/entity_dispatch.cpp`
- **No change required**: SpikeBlock entities are decoded directly in `level_loader.cpp` into `room.hazards` (not into `room.spawns`), so they never flow through `DispatchLevelEntities`. Document this in the loader's switch with a comment: `// SpikeBlock decoded into room.hazards; not dispatched.`

##### `src/user/gameplay/scene/gameplay_scene.cpp`
- After motor/controller step and before leaving the fixed-step loop, call `CheckHazardKill(room, player.position, player.radius)`. On true:
  ```cpp
  impl_->respawn_system.ForceRespawn(
      impl_->player, impl_->checkpoint, impl_->room, impl_->player_motor);
  did_respawn = true;
  impl_->player.prev_position = impl_->player.position;
  ```
- This deliberately reuses the scene's existing `did_respawn` camera-reset path later in `Update(...)`.

##### `tests/scene_update_order_smoke.cpp`
- Add a hazard-triggered respawn case that starts from an obstructed old camera boom, calls the same scene-order helper used by the fall-respawn test, and requires spawn-frame framing to match the fresh-reset case. This keeps `.agents/common-mistakes/camera-respawn-reset.md` enforced by an automated test, not just manual ROM play.

#### Edge cases
- AABB is generous (worst-case for spike volumes). Inflate by `-2.0` game units on Y so the kill plane sits below the spike tip; tune to feel. Acceptable to ship a generous box for now.
- A SpikeBlock with `func_group` parent layer — already handled (func_group is skipped, its child entities are processed).

#### Verification
- Bake: confirm 6 hazard entries in entity table.
- ROM boot: walk into a spike, observe respawn at PlayerSpawn.
- Walk *past* a spike without touching: must not die.

---

### Inc 7 — DeathBlock kill volume (S)

**Depends on:** Inc 6
**Unblocks:** none
**Done criteria:** Falling below the level (into the DeathBlock AABB) kills the player and respawns.

#### Files to touch

##### `tools/entity_ids.py`
- Add `"DeathBlock": 6`.

##### `src/user/gameplay/world/entity_ids.hpp` (extend shared header from Inc 2)
- Add `kEntDeathBlock = 6`.

##### `tools/bake_map.py`
- Treat DeathBlock identically to SpikeBlock but with kind=1 (death). **DeathBlock emits NO faces** — only the entity record + AABB. Rationale: OG 1-1's DeathBlock extends to Quake y=4864 → game z=-972.8, which would overflow int16 packing even at `kPosFp=32`. Skipping face emission also avoids loading the kill plane onto the GPU.
- Move `"DeathBlock"` out of `UNSUPPORTED_BRUSH_CLASSES`.

##### `src/user/gameplay/world/level_loader.cpp`
- `kEntDeathBlock` → reuse `HazardVolume` with `kind = 1`. Same path as SpikeBlock.

##### `src/user/gameplay/actor/spike_actor.cpp` (or rename if you prefer)
- No code change needed; `CheckHazardKill` already iterates all hazards regardless of kind, and `RespawnSystem::ForceRespawn(...)` from Inc 6 is reused unchanged.
- Optional: log which kind on kill (`debugf("[hazard] killed by kind=%u", h.kind)`).

#### Edge cases
- DeathBlock in OG 1-1 is a giant box covering everything below y≈-12.8 game units. Verify the bake produces the correct half-extents.
- Player can dash into the DeathBlock from above through `TB_empty`-textured faces — they're visual-only at bake, so no static collision blocks the dash. Correct: death is triggered by the kill volume, not by collision.

#### Verification
- Walk off the edge of a platform: die within a second.
- Bake log: `hazard_count = 7` (6 spikes + 1 death).

---

### Inc 8 — TrafficBlock + Node path-follow on `Room::moving_surfaces` (L)

**Depends on:** Inc 3 and Inc 4 (string/props helpers plus Face `flags` field)
**Unblocks:** none
**Done criteria:** TrafficBlock platforms move between their authored Node positions, carry the player through the existing moving-surface path, and render as textured cuboids using `metal_floor_1`. No part of this increment depends on `ActorWorld`.

#### Files to touch

##### `tools/entity_ids.py`
- Add `"TrafficBlock": 7`, `"Node": 8`.

##### `src/user/gameplay/world/entity_ids.hpp` (extend shared header from Inc 2)
- Add `kEntTrafficBlock = 7`, `kEntNode = 8`.

##### `tools/bake_map.py`
- Pre-pass: build `nodes_by_targetname: Dict[str, Vec3]` from all `Node` entities.
- For each `Node` point entity:
  - Emit a normal entity at its transformed origin.
  - Pack `targetname_str_id` into `props_blob` as:
    ```
    uint16 targetname_str_id
    uint16 reserved
    ```
- For each `TrafficBlock`:
  - **Emit no faces and no static colliders.** Traffic platforms are dynamic runtime solids, so baking faces into the level mesh or colmesh would double up collision and render ownership.
  - Compute the authoring origin / AABB center and emit a `TrafficBlock` entity record.
  - Pack into `props_blob`:
    ```
    float half_extents[3]      // chosen gameplay size in game units
    uint16 target_node_str_id
    uint16 reserved
    ```
  - Keep the runtime size explicit and constant-driven in the baker so the N64 side does not need to infer dimensions from a marker brush.

##### `src/user/gameplay/world/world.hpp` (Room)
- Add fields:
  ```cpp
  static constexpr int kMaxNodes = 8;
  static constexpr int kMaxTraffic = 8;
  struct NodeSpawn {
      Vec3 position;
      uint16_t targetname_str_id;
  };
  struct TrafficSpawn {
      Vec3 origin;
      Vec3 half_extents;
      uint16_t target_node_str_id;
      int resolved_node_index = -1;
      int moving_surface_index = -1;   // filled by scene runtime init
      int owner_id = -1;               // stable collider/surface owner id
  };
  NodeSpawn nodes[kMaxNodes];
  int node_count = 0;
  TrafficSpawn traffic_blocks[kMaxTraffic];
  int traffic_count = 0;
  ```

##### `src/user/gameplay/world/level_loader.cpp`
- `kEntTrafficBlock` → decode `props_blob` slice (`float[3] half_extents`, `uint16 target_node_str_id`, 2 bytes pad). Push to `room.traffic_blocks` with `resolved_node_index = -1`, `moving_surface_index = -1`, `owner_id = -1`.
- `kEntNode` → push to `room.nodes` with the entity origin and decoded `targetname_str_id`.
- After all entities are read, run a second pass: for each `traffic_blocks[i]`, walk `room.nodes[]` and set `resolved_node_index` to the first node whose `targetname_str_id` matches the traffic block's `target_node_str_id`.
- Log unresolved targets: `[lvl] warning: TrafficBlock %d target unresolved`.

##### `src/user/gameplay/render/traffic_block_renderer.hpp` and `.cpp` (NEW)
- Add a tiny dedicated renderer for up to `Room::kMaxTraffic` textured cuboids.
- Responsibilities:
  - Build a reusable box mesh with valid UVs for a repeating `metal_floor_1` material.
  - Own one `T3DMat4FP*` per live platform instance.
  - Expose `Init(const Room& room)`, `UpdateInstance(int index, const Vec3& position, const Vec3& half_extents)`, `Draw(const MaterialCatalog& catalog)`, and `Free()`.
- This is intentionally separate from `ActorWorld`; it is a scene-owned render helper for dynamic level geometry.

##### `src/user/gameplay/scene/gameplay_scene.cpp`
- Add fixed runtime state on `Impl`:
  ```cpp
  struct TrafficRuntime {
      int traffic_index = -1;
      float phase_t = 0.0f;
      float period_seconds = 4.0f;
      Vec3 waypoint_a;
      Vec3 waypoint_b;
      bool has_path = false;
  };
  TrafficRuntime traffic_runtime[Room::kMaxTraffic];
  int traffic_runtime_count = 0;
  TrafficBlockRenderer traffic_block_renderer;
  ```
- Level init / reload path:
  - After `LoadLevel(...)`, build one `MovingSurface` + six box colliders per `room.traffic_blocks[i]`.
  - Allocate a stable `owner_id` per platform, append a `MovingSurface` to `room.moving_surfaces`, and append colliders to `room.colliders` once. Do **not** re-add them every frame.
  - Populate `traffic_runtime[]` from the resolved node pairs and call `traffic_block_renderer.Init(room)`.
- Fixed-step update path:
  - Before the existing `AdvanceMovingSurfaces(room, dt)` call, update each `traffic_runtime[i].phase_t`, compute eased interpolation between `waypoint_a` and `waypoint_b`, and write the new target position into `room.moving_surfaces[moving_surface_index].position`.
  - The existing `AdvanceMovingSurfaces(...)` implementation then computes displacement, updates collider bounds, and exposes `owner_velocity` to the player motor.
- Render path:
  - After the baked level draw and before actor models, call `traffic_block_renderer.Draw(impl_->material_catalog)`.
  - Refresh each instance matrix from the current moving-surface position so render matches collision.
- Reload path:
  - Clear `traffic_runtime_count`, `room.moving_surface_count`, and any traffic-owned dynamic colliders before rebuilding them from the freshly loaded room.

##### `src/user/gameplay/world/entity_dispatch.cpp`
- No functional change required. Add a comment that `TrafficBlock` and `Node` are decoded into dedicated `Room` arrays and never flow through `ClassnameToPlaceholder(...)`.

#### Edge cases
- TrafficBlock with unresolved `target` (Node missing) — keep its `MovingSurface` parked at `origin` and log once.
- Multiple TrafficBlocks pointing at the same Node — supported; each runtime entry has its own phase and owner id.
- Dynamic-collider lifetime — traffic colliders are installed once per level init/reload and moved via `AdvanceMovingSurfaces(...)`; they are not appended per frame.
- Player riding a moving block: existing platform-carry code in `player_motor.cpp` already consumes `owner_velocity` through `MovingSurface`.

#### Verification
- Bake log: `traffic_count = 5, node_count = 5`, all targets resolved.
- ROM boot: observe blocks oscillating.
- Ride a block: stays under the player without sliding.
- Add a host smoke or scene-order test that steps one traffic platform for several fixed ticks and verifies both `room.moving_surfaces[*].position` and the owned collider bounds move together.

---

### Inc 9 — Cassette + level-end (M) [done]

**Depends on:** Inc 3
**Unblocks:** none
**Done criteria:** A Cassette icon appears at the OG origin. Player overlap triggers a fade-to-black + level-restart (no follow-up level exists; restart is the closest faithful action).

#### Files to touch

##### `tools/entity_ids.py`
- Add `"Cassette": 9`.

##### `src/user/gameplay/world/entity_ids.hpp` (extend shared header from Inc 2)
- Add `kEntCassette = 9`.

##### `tools/bake_map.py`
- Point entity: emit Cassette as a normal entity with `origin`.

##### `src/user/gameplay/world/level_loader.cpp`
- `kEntCassette` → store `Room::cassette = origin; Room::has_cassette = true`.

##### `src/user/gameplay/world/world.hpp`
- Add `Vec3 cassette; bool has_cassette;`.

##### `Makefile`
- Add `tape_1.t3dm` to `DFS_MDL_FILES` (the rolling-cassette OG model).

##### `src/user/gameplay/actor/cassette_actor.hpp` and `.cpp` (NEW)
- Simple: renders a `tape_1` model at `room.cassette` with a slow Y-axis spin (animation phase from `time::SecondsSinceBoot()`).
- Tick: if `Vec3Distance(player_pos, room.cassette) < 1.2`, set a `cassette_collected = true` flag in scene.

##### `src/user/gameplay/scene/gameplay_scene.cpp`
- Construct/draw/tick CassetteActor when `room.has_cassette`.
- On `cassette_collected`: trigger a fade-out (stub: 500 ms hold), then reload the level. **Do not call `LoadLevel` directly on the existing `Room` — it leaks the `CollMesh`, `LevelRenderer` vert buffer, `MaterialCatalog` sprites, and (Inc 5) `PropRenderer` matrices/models.**
- Required reload sequence:
  ```cpp
  prop_renderer.Free();
  traffic_block_renderer.Free();
  level_renderer.Free();
  material_catalog.Unload();
  if (room.coll_mesh) { physics::FreeCollMesh(room.coll_mesh); room.coll_mesh = nullptr; }
  // Reset Room to default state (memset/placement-new), clearing
  // hazards/nodes/traffic/props/cassette_collected and spawn count.
  room = Room{};
  LoadLevel("rom:/lvl/1-1.lvl", room, level_geometry);
  material_catalog.Load("1-1");
  level_renderer.Init(level_geometry);
  actor_world = ActorWorld{};
  DispatchLevelEntities(room, actor_world, strawberry_actor, refill_actor, spring_actor);
  actor_world.ResolvePending();
  prop_renderer.Init(room);
  ```
- Rebuild the traffic runtime immediately after the reload path so dynamic platforms and their colliders are reinstalled before the next fixed tick.

#### Edge cases
- Cassette overlap radius (1.2 game units) approximates OG pickup feel. Tune after seeing it.
- Player overlap during dash — still counts.
- If player dies between spawn and cassette pickup, the cassette remains pickupable on the next attempt.

#### Verification
- Bake log: `cassette baked at (X, Y, Z)`.
- ROM boot: tape spins at OG location. Touching it triggers fade + reload.
- Extend `tests/scene_update_order_smoke.cpp` with a reload-adjacent respawn/camera assertion if the fade path shares the same reset logic.

---

## Cross-cutting verification

After all increments land, run a full-level playthrough on hardware (or in emulator):

1. Spawn at PlayerSpawn (OG: `32 120 384` → game ≈ `6.4, 76.8, -24`).
2. Walk past first SpikeBlock layout — must die on touch.
3. Activate TrafficBlock — observe oscillation toward Node.
4. Ride TrafficBlock across the gap, dismount.
5. Climb past the next spike group; collect Strawberry mid-jump.
6. Reach the Cassette; observe pickup → fade → reload.
7. Confirm DeathBlock catches a deliberate jump-into-the-pit.
8. Visually confirm: snow lumps (Decorations), trees/bushes/grass (StaticProps), correct material on each surface.

Document the playthrough in a follow-up note (`docs/1-1-playthrough.md`) if the team wants a regression check.

## Standards / common-mistakes referenced

- `.agents/common-mistakes/og-map-polygon-winding.md` — applies to Inc 2 (re-baking OG geometry). Verify winding still produces outward normals; the existing test `tests/world_query_parity_test.cpp` covers this.
- `.agents/common-mistakes/camera-respawn-reset.md` — applies to Inc 6, 7, 9. Camera state must reset cleanly on kill/respawn/level-end fade, and `tests/scene_update_order_smoke.cpp` should be extended alongside those increments.
- No `.agents/standards/` directory exists yet; standards are encoded in CLAUDE.md and the codebase conventions. Honor the existing big-endian binary format style and dual-side mirroring between `tools/*.py` and `src/user/.../*.hpp`.

## Open questions (CONSIDER from review)

- **Total `GameplayScene::Impl` struct size after capacity bumps.** Inc 2 grows `LevelGeometry` by ~110 KB and adds ~50 KB more from Inc 5/6/8 Room fields. Worth a sanity print of `sizeof(Impl)` after Inc 2 to confirm RDRAM headroom.
- **`tests/world_query_parity_test.cpp` loads `first-room.lvl` (line 35).** After the level swap this test still runs against first-room, which is arguably the right regression coverage — but worth a decision: keep as first-room regression, or migrate to 1-1.
- **Node `target` strings are numeric** ("000"–"004"). Bake-time string-id compare is exact-equality; document this so a future baker change (leading-zero stripping, integer normalization) doesn't silently break path resolution.
- **`tape_1.t3dm` provenance** — assumed to come from `assets/og_converted/models/tape_1.t3dm` (the standard pattern). Confirm presence before Inc 9.
- **Tests for hazard/traffic/prop bakes**. The plan covers shell smoke tests but not entity-table content tests. A bake-side asserttion that `hazard_count == 7` after baking 1-1 would catch a baker regression cheaply.

## Out of scope

- Real audio playback (music/ambience). Inc 3 wires strings only.
- Snow particle rendering. Inc 3 stores params only.
- Skybox rendering (per-room sky). Inc 3 stores skybox name only.
- StaticProp collision (height/radius fields are stored but unused). Trees/bushes are passable.
- Other 1-x rooms — this plan converts only 1-1.
- Animated cassette pickup VFX. Just a fade.
- TrafficBlock multi-node looping paths beyond a two-point oscillation.
- Map editor / re-export from TrenchBroom — out of scope.
- Performance optimization of the renderer (per-material batching) — deferred to a future plan.
