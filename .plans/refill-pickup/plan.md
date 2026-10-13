# OG refill actor lifecycle and pickup parity

## Context

The player now has OG integer dash charges, but the real map cannot grant them: actor pickup dispatch is absent, repeated same-type spawns alias one singleton object, and the map-pack spawn table drops `Refill.double`. Current C++ refill values also differ from source (radius 15 vs 20, cooldown 3 s vs 4 s), and `ActorWorld::Update` skips inactive actors, preventing its intended refill cooldown from advancing. Close complete real-map refill lifecycle without narrowing the active full-fidelity goal.

OG contract: `Player.Update` tests `SolidWaistTestPos` against each `IPickup.PickupRadius` with strict `< radius²`; refill accepts only when `dashes < (IsDouble ? 2 : 1)`, raises charges, hides for 4 s, then restores; a successful pickup sets `World.HitStun = .05f`. OG world hitstun skips the whole world update, not only player movement. Source: `Celeste64-og/Source/Actors/Player.cs:162-183,378-386`; `Actors/Refill.cs:12-16,27,51-84`; `Scenes/World.cs:348-352`.

## Architectural decisions

- Decision: add a versioned MPP3 spawn record with one compact actor-flags byte; keep MPP2 decoding and normalize missing flags to zero. Rationale: preserve existing content while carrying currently consumed `Refill.double` behavior. This is selective metadata, not lossless arbitrary-property storage. Alternatives rejected: encode the needed flag into `name`/`classname` strings or infer from source entity index. Source: authored properties survive in `tools/ogworld/model.py:125-141`, then disappear in `tools/mappack_format.py:375-405`.
- Decision: use fixed-capacity owned actor slots, bounded by existing 64 actor slots; no per-spawn heap allocation. Retain external `ActorWorld::Add` for fixtures/non-owned actors. Rationale: stable pointers, deterministic N64 memory, and real multi-instance behavior. Source: `ActorWorld::kMaxActors` and current external-pointer registry in `src/user/gameplay/world/actor_world.hpp:6-25`.
- Decision: create supported map actors once from all manifest rooms at map boot and preserve instances across cell transitions. Current map has 34 Strawberry/Refill/Spring records, under the 64-slot cap. This preserves cooldown/collected state like OG's world-lifetime actors; fail visibly if future maps exceed cap. Legacy `.lvl` path stays per-room and defaults refill amount one. Source: OG `World` owns all map actors for world lifetime; `MapSpecV2` retains every room spawn and this map contains 20 Strawberries + 8 Refills + 6 Springs.
- Decision: route dash and refill hitstun through scene-level whole-world tick gating. Keep duration requests ordered within a tick so a refill's `.05 s` assignment after dash's `.02 s` assignment wins, matching source write order. Source: OG writes `.02 s` for dash at `Celeste64-og/Source/Actors/Player.cs:1250-1258`, `.05 s` for refill at `Actors/Refill.cs:74-85`, and early-returns whole World.Update at `Scenes/World.cs:348-352`.
- Decision: keep refill bobbing in render transform only; never move pickup center during bob. Source: OG `Refill.Render` offsets the model while `Refill.Position` stays fixed (`Celeste64-og/Source/Actors/Refill.cs:51-72`); current `BobbingActor::UpdateBobbing` mutates gameplay position.

## Assumptions and answers from code

- Answered from code: source `SpawnRecord.properties` already retains entity property dictionaries — `tools/ogworld/model.py:125-141`; bake currently copies only classname/name/position — `tools/bake_interconnected_map.py:246-255`.
- Answered from code: binary MPP2 record serializes no actor properties and C++ rejects non-v2 version — `tools/mappack_format.py:290-327,375-425`; `src/user/gameplay/world/mappack_loader.cpp:220-245`.
- Answered from code: `MapRuntime::ActiveSpawns` already converts manifest actor records but has no callers — `src/user/gameplay/world/map_runtime.cpp:199-229`; scene instead dispatches the active `.lvl` spawns — `src/user/gameplay/scene/gameplay_scene.cpp:568-570,767-770`.
- Answered from code: same Strawberry/Refill/Spring objects are reused for every spawn — `src/user/gameplay/world/actor_factory.cpp:3-12`; `ActorWorld::Update` also filters `actor.active` — `src/user/gameplay/world/actor_world.cpp:10`.
- Answered from code: project checks are `./tests/run_host_tests.sh` and `./compile-rom.sh`, documented in `AGENTS.md`.
- Assumption: MPP2 remains readable but all new bakes emit MPP3. Source: existing reader has strict version gate and all new ROMs package generated local map assets; no external consumer contract is present in repo.
- Assumption: current implemented player states are pickup-capable; future OG-excluded states must add explicit gates when implemented. Source: current `PlayerMovementState` has Normal, Dashing, Climbing, Skidding only — `src/user/gameplay/player/player_state.hpp:55-61`; OG exclusions are StrawberryGet, Bubble, Cassette, StrawbReveal, Respawn, Dead — `Player.cs:177-183`.

## Risks accepted

- Fixed pool adds bounded static scene memory. Mitigation: pool 64 slots, compile-time size/alignment checks, record `sizeof(ActorWorld)` and ROM BSS/memory budget; fail compilation if actor exceeds slot contract.
- ActorWorld has 64 map-wide supported-actor cap. Mitigation: current map has 34; test exact pool limit and overflow rejection; fail visibly rather than silently dropping actors.
- Whole-world hitstun changes actor/camera/input tick order. Mitigation: centralize gating at fixed-step tick entry and add timing tests proving all actors/player freeze for source duration, with the triggering tick still completing.
- No new Ares capture while user owns an interactive session. Mitigation: host and ROM proof now; device visual/telemetry proof remains an explicit future gate after user closes that session.
- MPP3 stores only the current refill behavior bit, not arbitrary OG entity properties. Cassette map/angle and Strawberry target metadata remain explicit fidelity gaps for later scope.

## Increment DAG

- Inc 1 — Preserve refill spawn metadata (M) — depends on: none — unblocks: 2
- Inc 2 — Give each spawn stable actor instance (M) — depends on: 1 — unblocks: 3, 4
- Inc 3 — Match refill pickup and hitstun physics (M) — depends on: 1, 2 — unblocks: 4
- Inc 4 — Render refill lifecycle and qualify ROM (M) — depends on: 1, 2, 3 — unblocks: interactive visual/device proof

## Increments

### Inc 1 — Preserve refill spawn metadata (M)
**Depends on:** none
**Unblocks:** Inc 2
**Done criteria:** MPP3 round-trip preserves three authored `double=1` refill entities; MPP2 loads with flags zero; runtime manifest exposes amount 1 or 2.

#### Files to touch

##### `tools/mappack_format.py`
- What changes: add MPP3 writer/reader with `actor_flags:u8` appended to each spawn record; retain MPP2 reader and normalized runtime record defaults.
- Function(s): `write_mappack_v3_binary(pack, path)`, `read_mappack_v3_binary(path)`, legacy v2 reader.
- Data shapes: `V3Spawn.actor_flags` bit 0 = double refill; all other bits rejected until defined; non-Refill records require zero flags.
- Integration points: `tools/bake_interconnected_map.py`; fixture tests.
- Error paths: reject unknown flag bits, truncation, invalid spawn counts and invalid flag/classname combinations.

##### `tools/bake_interconnected_map.py`
- What changes: derive double-refill flag from `SpawnRecord.properties["double"] > 0`; emit MPP3.
- Function(s): map per-source spawn properties while constructing `V3Spawn`.
- Data shapes: source properties remain strings in canonical IR, serialized as compact actor flag.
- Integration points: deterministic bake and MPP3 writer.
- Error paths: malformed numeric property fails with entity source ID; missing/zero property means normal refill.

##### `src/user/gameplay/world/mappack_loader.hpp` and `mappack_loader.cpp`
- What changes: expose normalized spawn flags; accept MPP2 and MPP3, decode old records as flags zero.
- Function(s): generic `LoadMapPack`; remove strict version-2-only parse assumption.
- Data shapes: normalized `SpawnSpec.actor_flags:uint8_t`.
- Integration points: `MapRuntime` and actor dispatch.
- Error paths: reject unsupported magic/version, unknown flags, over-capacity counts, and short records; no partial successful load.

##### `tests/malformed_mappack_v2_test.py`, `tests/mappack_loader_smoke.cpp`, `tests/interconnected_map_smoke.py`
- What changes: keep old v2 compatibility tests and add v3 properties/flags, malformed flags, byte truncation, and real-map source-entity assertions.
- Error paths: parser must fail closed without interpreting malformed bytes as spawn records.

#### Edge cases

- Legacy MPP2 refill defaults to one charge.
- `double` absent, empty, or zero means one; positive means two; invalid text fails bake with source entity identity.
- Start/anchor and non-Refill records cannot carry refill flags.
- Maximum room spawn count remains 64; 65th spawn fails during bake/parse.

#### Verification

- Run: focused Python format/bake tests, `./tests/run_host_tests.sh`.
- Tests: MPP2 backward read; MPP3 double-refill round-trip; malformed/unknown flags rejected; baked map contains three double refills.
- Done: source `double` values survive canonical bake, MPP3 bytes, C++ decode, and normalized `ActorSpawn.actor_flags` conversion in `MapRuntime::ActiveSpawns`.

#### Status / evidence

- Complete: focused Python MPP3 tests pass (round-trip, MPP2 zero-default, unknown/invalid flags and truncation).
- Complete: C++ loader fixture passes for MPP2 + MPP3 and rejects bad flags, non-Refill flags, and truncated records.
- Complete: `python3 tests/interconnected_map_smoke.py` passes; baked real map contains 3 double and 5 normal refills.
- Complete: `./tests/run_host_tests.sh` passes 113/113, including existing MPP2 staging loader/runtime checks.
- Inc 2 integration gate: production dispatch must use all manifest rooms once and copy flags to distinct owned actors.

### Inc 2 — Give each spawn stable actor instance (M)
**Depends on:** Inc 1
**Unblocks:** Inc 3, Inc 4
**Done criteria:** map-wide same-type spawns retain distinct positions, source IDs, and lifecycle across cell transitions; all instances participate in interaction/render iteration; pool exhaustion is explicit.

#### Files to touch

##### `src/user/gameplay/world/actor_world.hpp` and `actor_world.cpp`
- What changes: fixed 64-slot owned storage, typed emplacement/iteration, destructor-safe clear/remove, and explicit opt-in update for actors with timers that continue while hidden (refill cooldown).
- Function(s): `template<class T> T* EmplaceOwned()`, `ForEachType<T>(Fn)`, `Clear()`, `At/Count` as needed.
- Data shapes: aligned bounded byte slots plus typed destructor callbacks; external `Add(Actor&)` remains non-owning.
- Integration points: actor factory, scene transitions, update, pickup/render loops.
- Error paths: compile-time size/alignment failure; full pool returns failure and logs once; no pointer aliasing or double destruction.

##### `src/user/gameplay/world/actor_factory.hpp/.cpp`, `entity_dispatch.hpp/.cpp`, `world.hpp`
- What changes: factory emplaces a unique Strawberry/Refill/Spring per spawn and copies source ID/refill flags.
- Function(s): `SpawnActor(ActorWorld&, const ActorSpawn&)`; preserve legacy class-ID translation in dispatch.
- Data shapes: `ActorSpawn` gains normalized `actor_flags`.
- Integration points: legacy room loader and map-pack manifest spawn path.
- Error paths: unknown class and capacity exhaustion return explicit errors; no shared singleton fallback.

##### `src/user/gameplay/world/map_runtime.cpp`, `gameplay_scene.cpp`
- What changes: add all-manifest actor enumeration and dispatch once at map boot; use `ForEachType` for all springs and strawberries; preserve actors across cell transitions.
- Function(s): map-wide dispatch helper; `TransitionToRoom` leaves actor ownership intact; render/update iteration.
- Data shapes: actor pool owns up to 64 supported map actors; current map contributes 34.
- Error paths: map boot fails visibly on actor-capacity failure; no actors are silently dropped or reset on room transition.

##### `tests/actor_identity_smoke.cpp`, `tests/actor_type_id_smoke.cpp`
- What changes: prove same-type spawns have different addresses/state, stable source IDs, correct destruction/reuse, and iteration includes dormant cooldown actors.

#### Edge cases

- ActorWorld clear destroys owned objects once and never destroys external `Add` references.
- Removal of one actor does not shift IDs or invalidate other actor pointers.
- Room transition preserves actor pointers, cooldowns and collected state; scene teardown clears ownership.
- Dormant collectible updates cooldown but does not render or accept pickup.

#### Verification

- Run: focused actor tests, `./tests/run_host_tests.sh`, `./compile-rom.sh`.
- Done: all actor instances in test room are unique and `ActorWorld` footprint remains within declared budget.

#### Status / evidence

- Complete: map-wide dispatch creates 34 distinct current-map actor instances (20 strawberries, 8 refills, 6 springs); source IDs and refill flags survive room transitions.
- Complete: `actor_owned_pool_smoke` and `map_runtime_v2_smoke` pass in the final **117 passed, 0 failed** host suite.
- Complete: ROM rebuild succeeds; map-pack output was not rebaked or overwritten.

### Inc 3 — Match refill pickup and hitstun physics (M)
**Depends on:** Inc 1, Inc 2
**Unblocks:** Inc 4
**Done criteria:** real/synthetic overlap grants source amount only when below it, uses strict waist-radius threshold and 4-second cooldown, and pauses all world simulation for 0.05 seconds.

#### Files to touch

##### `src/user/gameplay/actor/refill_actor.hpp/.cpp`
- What changes: preserve per-spawn double flag, source pickup radius 20, amount-aware eligibility, 4-second cooldown/hidden lifecycle, and source bob/collect timing fields.
- Function(s): `DashAmount()`, `CanPickup(int dashes)`, `OnCollect()`, `Update(float dt)`.
- Data shapes: `dash_amount` = 1 or 2; cooldown and collect timers are fixed-step seconds.
- Integration points: scene pickup dispatch; actor update/render.
- Error paths: cannot collect when inactive, cooling down, or player already has enough charges.

##### `src/user/gameplay/scene/gameplay_scene.cpp`, `player_state.hpp`, `player_controller.cpp`, `world/refill_pickup.hpp`
- What changes: run OG waist-point pickup check after player movement; route dash/refill hitstun through whole-world tick gate; keep ordered last-write-wins hitstun requests.
- Function(s): `ProcessRefillPickups`, fixed-step world tick gate, request consumption; controller sets `.02s` dash request and refill sets `.05s`.
- Data shapes: world hitstun timer and pending request are distinct; request resets only after the tick completes.
- Integration points: actor iteration, `RefillDash(amount)`, fixed-step actor/camera/player updates.
- Error paths: no duplicate pickup in same cooldown; no player/actor/camera progression during hitstun; no drop of remaining accumulator time.

##### `tests/refill_pickup_parity_smoke.cpp`, `tests/scene_update_order_smoke.cpp`
- What changes: cover waist offset, `<20` vs `==20`, one/double charge gating, full-charge no-op, cooldown, `.05s` whole-world freeze, dash `.02s` freeze, and timer write ordering.

#### Edge cases

- One-charge refill does nothing at one or more charges; double refill does nothing at two or more.
- Overlap at exactly radius 20 does not collect.
- Pickup starts cooldown/hitstun only on successful charge change.
- Hitstun freezes player, actors, camera and world timers for source-duration ticks; triggering tick completes before freeze begins.
- Refill cooldown timer does not progress during world hitstun.

#### Verification

- Run: focused host fixtures, `./tests/run_host_tests.sh`, `./compile-rom.sh`.
- Done: source-derived 60 Hz traces match charge count and pause tick sequence.

#### Status / evidence

- Complete: `refill_pickup_parity_smoke` covers waist anchor, strict `<20` overlap, normal/double eligibility, no-op when full, 4-second hidden cooldown, 60 Hz `.05s` freeze (3 paused ticks), input-edge retention, and last-write-wins `.02s`/`.05s` requests.
- Complete: scene gate precedes simulation input buffering; camera, player, actors, and timers execute only on unpaused fixed ticks. Hair now receives one fixed constraint update per active tick and none on paused-only frames.
- Complete: `./tests/run_host_tests.sh` passes **117/117**; ROM builds successfully. Current user-controlled Ares session was not disturbed; device/manual behavior remains open.

### Inc 4 — Render refill lifecycle and qualify ROM (M)
**Depends on:** Inc 1, Inc 2, Inc 3
**Unblocks:** interactive visual/device proof
**Done criteria:** normal/double refill models render at every active spawn, disappear on collection, return after 4 seconds, and identify build/map/actor telemetry; no live Ares session is disrupted.

#### Files to touch

##### `src/user/gameplay/scene/gameplay_scene.cpp`, `src/user/gameplay/actor/refill_actor.hpp/.cpp`
- What changes: load already-manifested normal/double refill models; match OG scale, bob, spin, visibility/cooldown lifecycle; render all active refill actors.
- Function(s): actor render loop / transform sampling.
- Data shapes: shared model per variant, per-actor transform and cooldown.
- Integration points: asset manifest paths `rom:/mdl/refill_gem.t3dm` and `refill_gem_double.t3dm`; fixed-step actor time.
- Error paths: required models fail visibly in loader telemetry; no silent missing-model substitution.

##### `tests/refill_render_contract_smoke.py`, `docs/og_behavior_gap_audit.md`, `docs/og_port_gap_matrix.md`, `.plans/full-og-fidelity/plan.md`
- What changes: assert both model paths, all-actor rendering contract and source transform parameters; record residual device gate and exact ROM identity.

#### Edge cases

- Camera-relative position is used in open-world map mode.
- Collected/cooling refill is neither drawn nor pickup-eligible.
- Normal and double actors remain distinct when adjacent or present multiple times.

#### Verification

- Run: `./tests/run_host_tests.sh`; `./compile-rom.sh`; confirm exact ROM SHA and assets packaged.
- Device: defer Ares capture/manual visual review until interactive Ares is closed; never launch a second emulator alongside it.
- Done: code/host/build gates pass; device visual result remains explicitly open until captured.

#### Status / evidence

- Complete: normal/double T3DM assets are required and packaged at `rom:/mdl/refill_gem.t3dm` and `rom:/mdl/refill_gem_double.t3dm`; scene loads/frees each shared model and draws every active matching actor instance.
- Complete: OG source transform values use scale `2/64`, bob `sin(timer*2)*2`, spin `timer*3`; gameplay pickup center remains unbobbed. `refill_render_contract_smoke.py` passes.
- Complete: final full host suite **117 passed, 0 failed**. ROM size **1,753,088 bytes**, SHA-256 `c0d07cac5b25d479c67c93b6d96537902daa1c298d3a371724059be4a099479a`; build log confirms both refill models in DFS image.
- Device gate open: no new Ares launch/capture because interactive session belongs to user. This code/host/build result does not prove visible behavior on emulator or human movement feel.

## Cross-cutting verification

- Verify generated Forsaken City pack through source entity → canonical spawn properties → MPP3 bytes → C++ map-wide actor spawn → unique actor instance → waist overlap → dash count/hair state → hitstun/cooldown → rendered model.
- Rebuild map pack only after checking generated output targets for preexisting user modifications; preserve unrelated dirty artifacts.
- Keep active full goal open; this subsystem does not establish complete player, camera, skin, hair, hazard, animation, or source-world parity.
- Manual Ares evidence is separate from host/build evidence; current interactive session belongs to user and must not be stopped or replaced.

## Standards / common-mistakes referenced

- No `.agents/standards/index.yml` exists in this checkout.
- `.agents/common-mistakes/mid-refactor-working-tree.md` — preserve unrelated incomplete user work; don't mix it into this slice.
- `.agents/learnings/controls-physics-port.md` — preserve source tick order and use empirical fixtures, not dead-code gates.
- `.agents/learnings/memory-opt-pooling-and-budget.md` — bounded pools, alignment, and real memory accounting.
- `.agents/learnings/og-n64-asset-conversion-runtime.md` — manifest paths and Ares visibility proof.

## Open questions (CONSIDER from review)

- None yet.

## Out of scope

- Does not redefine full-fidelity acceptance or declare broader goal complete.
- Audio/particle export parity remains separate because available source sound banks are not packaged as N64-ready event assets; track in the global OG gap ledger.
- Feather, Bubble, strawberry/cassette/cutscene state behavior, hazards and all remaining movement/animation gaps remain in `.plans/full-og-fidelity/plan.md` and `docs/og_behavior_gap_audit.md`.
