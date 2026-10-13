# Dynamic Solid Parity

## Context

Close a high-impact physics gap in the active full OG-fidelity goal: OG `Solid` actors move, transform their collision faces, update `World.SolidGrid`, and carry `IRidePlatforms`; the current whole-map bake retains dynamic-class brushes as global CMSH collision proxies. Inc 4 now moves their visual geometry to actor-local DSOL rendering, while actor state/motion remains open. `TrafficBlock`, `FallingBlock`, `FloatyBlock`, `MovingBlock`, `GateBlock`, `CassetteBlock`, `BreakBlock`, and `DoubleDashPuzzleBlock` remain explicitly identified as dynamic collision proxies in `tools/ogworld/class_policy.py`. Current map has **118** dynamic-solid entities: 9 TrafficBlocks, 68 FallingBlocks, 22 FloatyBlocks, 3 MovingBlocks, 15 BreakBlocks, and 1 DoubleDashPuzzleBlock (counts from `assets/og_converted/maps/1.map`). The NAV sidecar is offline-only and not loaded by runtime.

Goal remains exact source behavior within the N64 runtime, not merely a plausible moving AABB. Static world, actor-owned collision, rendering, update order, and rider carry must agree. Preserve the larger `.plans/full-og-fidelity/plan.md` objective; this subplan closes only dynamic-solid behavior.

## Architectural decisions

- Decision: preserve each dynamic `Solid`'s full authored local faces and source identity; never replace collision with an AABB approximation. Rationale: OG queries finite faces and actor transforms can move faces outside their initial bounds. Alternatives rejected: moving only the current static proxy or using a box collider. Source: `Celeste64-og/Source/Actors/Solid.cs:32-75`; `World.cs:482-553`.
- Decision: retain one global CMSH for static geometry and use existing `CollSolidGridRecord` identity to exclude actor-owned faces from static queries and query them through live actor transforms. Rationale: avoids a second copy of collision geometry and preserves established world-mesh lifetime; CMSH v2 already maps source-face ranges to OG solid ids. Rebuild or cache acceleration per actor only if measurements require it. Source: `src/user/gameplay/physics/coll_mesh.hpp:52-60`; `tools/writers/colmesh_world_writer.py`.
- Decision: add MPP5 metadata plus a versioned `DSOL` actor-geometry sidecar, retaining MPP2/MPP3/MPP4 readers. MPP5 records stable source entity/order, OG solid id, kind, final initial actor transform, CMSH collision-face range, DSOL render range, and typed class payload. DSOL stores OG `Solid.LocalVertices`/faces exactly: source geometry after base map transform and generated-bounds-center offset; runtime applies final `Solid.Position`/Facing, including entity-origin overrides. Collision remains referenced from global CMSH and is localized by inverse final initial actor transform. For target-based classes, bake the resolved target source id and transformed position into typed payload; preserve OG first recursively encountered `targetname` match and missing-target zero-position behavior. Source: `tools/mappack_format.py`; `src/user/gameplay/world/mappack_loader.cpp:270-285,383-423`; `Celeste64-og/Source/Data/Map.cs:355-370,497-541`.
- Decision: render dynamic-solid geometry from actor-local source faces under the same runtime transform as collision; remove it from static-cell visual output. Do not leave a moved collider paired with a frozen or duplicated visual proxy. Source: `tools/ogworld/geometry.py`, `tools/ogworld/chunking.py`, `src/user/gameplay/render/dynamic_solid_renderer.cpp`.
- Decision: preserve OG's two insertion-ordered actor passes: every actor `Update()`, then every actor `LateUpdate()`. Capture the frustum/offscreen view once immediately before pass one; Player camera changes during Update do not recalculate that tick's view. `Map.Load` visits entities in source order and `ResolveChanges` appends actors in that order. In current `1.map`, `PlayerSpawn Start` precedes all `TrafficBlock` records, so Player.Update runs before TrafficBlock.Update; Player.LateUpdate runs only after all actor updates. C++ must split player movement from late contact and merge player, point actors, and dynamic solids into one source-ordered schedule. `Solid.MoveTo` carries riders in actor insertion order before changing platform position. Preserve hitstop. Source: `Celeste64-og/Source/Data/Map.cs:278-340`; `World.cs:219-245,362-370`; `Solid.cs:76-80,122-149`; `src/user/gameplay/scene/gameplay_scene.cpp:1497,2231`; `src/user/gameplay/player/player_controller.cpp:1299-1434`; `assets/og_converted/maps/1.map:6206-6214`.
- Decision: give dynamic solids their own bounded map-wide registry, not `ActorWorld`'s 64 slots. Current map alone has 118 dynamic solids, and point actors also consume `ActorWorld`. Fail bake/runtime initialization on capacity overflow; never truncate. Source: `src/user/gameplay/world/actor_world.hpp:15-18`; current map classname counts from `assets/og_converted/maps/1.map`.
- Decision: sequence TrafficBlock and FallingBlock first, then FloatyBlock/MovingBlock/BreakBlock/DoubleDashPuzzleBlock; current map contains 9 and 68 instances of the first two and 41 of the next group. Source: current map classname counts; respective OG actor classes.

## Assumptions and answers from code

- The deliverable target is libdragon/tiny3d on N64; keep fixed-step and bounded-storage requirements. Source: `AGENTS.md` project/build and 8 MB target sections.
- Dynamic source brushes remain in the global collision artifact as actor-owned CMSH faces; Inc 4 now emits them only through DSOL for rendering, not static LVL cells. Source: `tools/ogworld/class_policy.py`, `tools/ogworld/geometry.py`, `tools/ogworld/chunking.py`.
- CMSH v2 already preserves `solid_id` and source-face ranges. Source: `src/user/gameplay/physics/coll_mesh.hpp:52-60` and `tools/writers/colmesh_world_writer.py`.
- MPP4 carries point actor spawns and hazard AABBs, but no moving-solid records; runtime accepts MPP2-4. Source: `tools/mappack_format.py`, `src/user/gameplay/world/mappack_loader.cpp:270-285,383-423`.
- Current `Room::MovingSurface` carries AABB colliders and rider velocity; it is not driven from map actors. Source: `src/user/gameplay/world/world.hpp:45-61,142-153`, `world.cpp:923-967`, and `PlayerController::MovingSurfacePhase`.
- Current runtime advances `MovingSurface` before player movement, and `MovePhase` currently includes `LateContactPhase` before `ActorWorld::Update`; this cannot match OG's insertion-ordered `Update` pass followed by insertion-ordered `LateUpdate`. Current map source order places Player before TrafficBlocks. Source: `src/user/gameplay/scene/gameplay_scene.cpp:1497,1514,2231`; `src/user/gameplay/player/player_controller.cpp:1299-1434`; `Celeste64-og/Source/Data/Map.cs:278-340`; `Celeste64-og/Source/Scenes/World.cs:362-370`; `assets/og_converted/maps/1.map:6206-6214`.
- `ActorWorld::kMaxActors` is 64, while 1.map has 118 dynamic solids; dynamic-solid ownership cannot be truncated into the existing point-actor array. Source: `actor_world.hpp:15-18`; map class counts.
- OG `Solid.MoveTo` transports `IRidePlatforms` before changing `Position`; `Solid.Update` moves by `Velocity * Time.Delta`. Source: `Celeste64-og/Source/Actors/Solid.cs:76-80,122-149`.
- NAV has no runtime consumer. `tools/bake_nav.py` emits an empty waypoint record for an unresolved target; legacy `tools/writers/nav_writer.py` fabricates a 20-unit target. Runtime target behavior must instead follow canonical source lookup: first matching named entity, or origin when missing. Source: `docs/room_artifact_contract.md:11-13`, `tools/bake_nav.py:31-47`, `tools/writers/nav_writer.py:32-53`, `Celeste64-og/Source/Data/Map.cs:355-370`.
- No unanswered architecture question changes the requested endpoint: user goal is correctness of animations and physics; `.plans/full-og-fidelity/plan.md:1-6` defines full source parity as the acceptance target, ruling out static proxies and approximate collision as final state.

## Risks accepted

- Dynamic face queries may cost more than the static BVH path. Keep candidate face sets bounded per solid, profile representative player/platform routes on Ares, then add actor-local acceleration if needed; do not silently drop candidates.
- Moving actors alter OG GridPartition insertion order after transform. Preserve live bounds and remove/reinsert ordering for exact tie behavior; stable source id alone is insufficient.
- New actor/render data increases 8 MB RDRAM and ROM footprint. Reuse canonical vertex/material data where possible and measure actual post-boot arena headroom.
- Old MPP/CMSH assets remain loadable. Reject malformed v5 dynamic records without partially replacing the active map.
- The current interactive Ares session is user-owned. Automated route helpers must refuse an already-running emulator; no validation step may close or replace it.

## Increment DAG

- Inc 1 — Dynamic-solid source records and versioned artifact (M) — depends on: none — unblocks: 2, 4.
- Inc 2 — Exact transformed-face collision overlay (L) — depends on: 1 — unblocks: 3.
- Inc 3 — OG actor scheduling, `Solid.MoveTo`, and rider ordering (M) — depends on: 2 — unblocks: 5, 6.
- Inc 4 — Dynamic-solid render cutover (L) — depends on: 1 — unblocks: 5, 6.
- Inc 5 — Current-map `TrafficBlock` runtime (M) — depends on: 1, 2, 3, 4 — unblocks: Ares end-to-end platform route.
- Inc 6 — `FallingBlock` trigger/fall/land/respawn lifecycle (L) — depends on: 1, 2, 3, 4 — unblocks: remaining actor-owned solids.
- Inc 7 — Remaining dynamic-solid classes and per-class behavior gates (L) — depends on: 1, 2, 3, 4, 5, 6 — unblocks: dynamic-solid portion of full fidelity audit.

## Increments

### Inc 1 — Dynamic-solid source records and versioned artifact (M)
**Depends on:** none
**Unblocks:** Inc 2, Inc 4
**Done criteria:** canonical bake emits explicit source-id/OG-solid-id records and actor-local geometry for all 118 current-map dynamic solids; record capacity is checked; existing ROM behavior and MPP2-4 loading remain unchanged.

**Status: complete — 2026-10-08.**

#### Files to touch

##### `tools/ogworld/model.py`, `tools/ogworld/parse.py`, `tools/ogworld/geometry.py`
- Add immutable typed `DynamicSolidRecord` fields: canonical source entity id/order (same domain as existing `V2SpawnSpec.source_id`), OG solid id, kind, final initial actor `Position`/Facing, CMSH solid/source-face range, DSOL vertex/face range, material/source-face identity, bounded class payload, and resolved target source id/position (or missing sentinel plus zero position) for target-based classes. Resolve links during bake using OG first-match source traversal; preserve source order with existing point-actor spawn records so runtime can merge one actor schedule. Keep arbitrary properties out of runtime records.
- Emit DSOL actor-local geometry and MPP5 references while retaining dynamic brushes in current static collision/render outputs until Inc 4 cutover.

##### `tools/mappack_format.py`, `src/user/gameplay/world/mappack_loader.hpp`, `src/user/gameplay/world/mappack_loader.cpp`, `src/user/gameplay/world/map_runtime.hpp`, `src/user/gameplay/world/map_runtime.cpp`
- Add MPP5 encode/decode for fixed-capacity dynamic records (`kMaxDynamicSolids >= 118`); include DSOL path/size/CRC and typed target source id/position in relevant class payloads. Parse into temporary metadata and publish only after complete validation; verify counts, unique source/solid ids, finite transforms/target positions, typed payload ranges, face ranges, path, CRC, exact file size, and trailing/truncated bytes. Preserve first matching `targetname`; missing target sentinel maps to zero position.
- Stage MPP5, global CMSH, and DSOL together during `MapRuntime::Init`; publish the new active map only after every artifact validates. Failure keeps any existing active map intact and frees staged allocations.
- Preserve MPP2-4 behavior and actor-spawn compatibility.

##### `tools/writers/dynamic_solid_writer.py`, `tools/dsol_format.py`, `src/user/gameplay/render/dynamic_solid_geometry.hpp`, `src/user/gameplay/render/dynamic_solid_geometry.cpp`
- Emit/load one versioned DSOL sidecar with contiguous actor-local Y-up vertex/face ranges per solid, triangle indices, authored source-face ids, material-catalog ids, and UVs. Reproduce `GenerateSolid`: apply base map transform, subtract generated bounds center, and preserve resulting `LocalVertices`; render with final actor `Position`/Facing. Validate ranges before replacing active geometry; bind material ids against existing map catalog.

##### `tests/mappack_v5_test.py`, `tests/og_dynamic_solid_record_smoke.py`, `tests/run_host_tests.sh`
- Cover all 118 current-map records and per-class counts, target first-match and missing-to-origin behavior, multi-brush solid grouping, CMSH↔DSOL source-face/pivot/transform roundtrip, source-order interleaving with MPP4 point spawns, dynamic/geometry capacity overflow, transactional malformed/truncated-load rejection, and legacy MPP2-4 reads.

#### Edge cases
- Several brushes form one `Solid`; entity origin/yaw and generated brush center must match `Map.LoadActor`.
- Target is missing, duplicated, or resolves first to a non-Node; preserve OG generic first-match lookup and zero-position fallback.
- CMSH and DSOL actor records disagree on source-face identity, local pivot, or transformed corner positions; reject bake/load.
- Dynamic actor crosses a visual cell boundary; it still exists once and is not duplicated by per-cell geometry.
- MPP record capacities overflow; bake/load fails rather than truncating.

#### Verification
- Run: `./tests/run_host_tests.sh`, canonical world bake, `python3 tests/colmesh_smoke.py`.
- Done: record counts and source hashes match a fresh bake; current static ROM artifact behavior remains unchanged.

#### Completion evidence
- Fresh current-map bake emits 118 source-ordered MPP5 records and DSOL actor-local meshes: 853 faces, 3,519 vertices, 134,404 bytes. Per-class counts: TrafficBlock 9, FallingBlock 68, FloatyBlock 22, MovingBlock 3, BreakBlock 15, DoubleDashPuzzleBlock 1.
- DSOL/CMSH source-face ranges, multi-brush ownership, final transform/pivot roundtrip, target first-match/missing-to-origin behavior, typed flags, capacity overflow, malformed/trailing inputs, and MPP2-4 compatibility have host coverage.
- Runtime validates MPP5 + CMSH + DSOL as a staged map before swapping active state. Sidecar CRC/size, CMSH CRC/size/counts, actor identities/ranges, and DSOL↔CMSH face ranges are checked. A failed reload keeps active map and collision intact.
- `./tests/run_host_tests.sh`: 132 passed, 0 failed. `tests/colmesh_smoke.py` passes CMSH v1/v2. Canonical bake passes. `./compile-rom.sh` passes; ROM SHA-256 `1d46eb528c1fbe3cdd62038df32c53b0824ad2718a9ab026ef8a8324e85e34c5`.
- No Ares session was launched or altered. Dynamic brushes remain in static proxy outputs until Inc 4 cutover; this increment adds records/sidecar only.

### Inc 2 — Exact transformed-face collision overlay (L)
**Depends on:** Inc 1
**Unblocks:** Inc 3
**Done criteria:** static queries no longer collide with dynamic-owned faces; ray, wall, floor, sweep, and climb queries hit those same finite faces at the actor's live transform with stable source identity/order.

**Status: complete — 2026-10-08.**

#### Files to touch

##### `src/user/gameplay/world/world.hpp`, `src/user/gameplay/world/world.cpp`
- Add bounded `DynamicSolidView` records referencing global CMSH solid ids, current transform/bounds, and update/reinsert ordinal.
- Add query dispatch for dynamic solids. Transform probe/ray into actor-local space, query all relevant source triangles (or a proven actor-local BVH), then transform hit point/normals back; apply the same one-way, front-side, plane-slice, and finite-face rules as static CMSH.
- Add dynamic-solid filtering to static `RaycastRoomSource`, floor/ceiling, wall/climb, and sweep paths. Avoid duplicate static+dynamic contact.
- Merge static and transformed actor candidates before every bounded hit cap using OG `GridPartition` ordering: 200-unit cells, 100×100 wrapped XY grid, one-unit cell-bound inflation, X-major/Y-minor query traversal, insertion-preserving bucket order, and first-encounter deduplication. Rank by first shared query cell, then actual bucket insertion ordinal (static CMSH `solid_id` for un-moved solids; runtime remove/reinsert serial for moved actors), then authored face ordinal. Keep live actor XY bounds and update reinsertion order after every transform; use OG X/−Z to port X/Z mapping and C# floor/ceil/wrap semantics. Source: `Celeste64-og/Source/Helpers/GridPartition.cs:10-67`; static-order precedent: `tools/ogworld/wall_order.py`, `src/user/gameplay/world/world.cpp:13-99,437-455`.

##### `src/user/gameplay/physics/coll_mesh.hpp`, `src/user/gameplay/physics/coll_mesh.cpp`
- Expose source-solid ownership and authored source-face lookup for every triangle; never infer ownership from bounds/coplanarity. Preserve face ids through fan triangulation, BVH reorder, CMSH serialization, and load.
- Collapse a face's fan triangles to one authored-face candidate; preserve source `LocalFaces` order within each Solid before query hit caps/first-hit behavior.

##### `tests/dynamic_solid_query_smoke.cpp`, `tests/run_host_tests.sh`
- Compare same face at initial and translated/yawed poses across query families; assert old pose misses after motion, new pose hits, fan triangles collapse to one candidate, authored face order wins within a Solid, back-side remains rejected, one-way policy remains, and static/dynamic ties follow OG first-shared-cell plus live remove/reinsert order.
- Assert OG X/−Z query-plane mapping, negative-coordinate floor/modulo, cell-boundary inclusion, 100×100 wrap seams, multi-cell-spanning actors, static/dynamic overlaps and candidate-cap ordering; static candidates use CMSH OG insertion `solid_id`, while moved actors use live per-cell append order.

#### Edge cases
- Multiple solids overlap one probe; moved solid leaves initial bounds and enters another 200-unit wrapped grid cell at positive/negative boundaries or across wrap seams.
- Two actor solids tie geometrically but move through different XY cells; BVH/source-face order must not override OG first-encounter bucket order after reinsertion.
- Static and dynamic solids overlap or span different shared cells; merged ranking must reproduce OG cell traversal then each bucket's actual insertion order.
- Solid has multiple coplanar source faces or fan-triangulated face; preserve OG face grouping and first hit.
- Transform has yaw and translation; up axis remains +Y, source map +Z maps to runtime +Y.
- Query touches exact radius/tangent boundary; preserve current tolerance contracts.

#### Verification
- `dynamic_solid_query_smoke` passes on current baked map: 118 actors / 853 faces; ray, wall, floor, ceiling, sweep, climb, moved-pose, proxy suppression, one-way, wrap seams, multi-cell, reinsertion, static/dynamic tie, and cap-order checks.
- `dynamic_solid_geometry_smoke` passes synthetic one-way floor/ceiling/wall/sweep checks.
- Full `./tests/run_host_tests.sh`: **133 passed, 0 failed**.
- `./compile-rom.sh` passes; ROM `madeline_cube_rom.z64`, 1,933,312 bytes, SHA-256 `2df9d2fac7f24309df209c9b570aec24442540dbd831e5243b5b1dbaf1b1ff99`.
- No Ares launch or interaction. Static render proxies remain until Inc 4; collision now uses transformed faces.

### Inc 3 — OG actor scheduling, `Solid.MoveTo`, and rider ordering (M)
**Depends on:** Inc 2
**Unblocks:** Inc 5, Inc 6
**Done criteria:** actor `Update()` and `LateUpdate()` execute in two OG insertion-ordered passes, with Player and all actor types occupying source order; camera/offscreen frustum snapshot occurs once before pass one; `Solid.MoveTo` carries eligible riders before transform/reinsert; hitstop freezes both solid and player; every launch site stores/consumes carry per source gates.

**Status: in progress — Player, point actors, active-room cassette, and dynamic-solid source slots interleave; room-local cassette lifecycle and device route evidence remain outside current host proof.**

`ActorWorld` dispatches point actors by canonical `source_id` before/after Player in both global passes, including one scene-owned actor slot for the active-room cassette. Map-pack Player movement splits at OG's boundary: sweeps/popout in `Update`, ground/snap/contact/presentation and camera follow in Player's ordered `LateUpdate` slot. `MovePhase` remains a complete-tick wrapper for standalone callers. `actor_owned_pool_smoke` and `actor_world_source_schedule_smoke` check source ordering; `scene_update_order_smoke` checks deferred grounding. The scene interleaves dynamic-solid slots with point actors and Player, applying OG base `Solid.Update` velocity displacement through rider-aware `MoveDynamicSolidTo`. The cassette timer runs at its manifest source slot in both passes; Player pickup uses the waist point and scaled OG radius. Shared visibility snapshot matches checked-in OG frustum inputs, and real-map host proof covers hitstop freeze/resume. Remaining: map-wide cassette persistence/target data, dynamic class-specific routines, and device route evidence.

**Progress update — 2026-10-08:** added actor-owned `MoveTo` carry using exact dynamic-face support identity. Grounded riders are checked through the shared `MoveService`; climbing riders match full-width owner identity. The moving solid is excluded from every dynamic query during the rider sweep, then transform/bounds/reinsert ordinal update after collision participation is restored. Query contacts expose live platform velocity. No-op moves update velocity without carry/reinsert. Carry state uses OG's exact zero-velocity and storage gates; both climb-corner anchors follow actual rider displacement. `dynamic_solid_rider_parity_smoke` exercises real baked geometry: grounded carry once, unsupported release, climb-owner carry, restored collision, live velocity, and no-op ordering. Launch carry now has source assertions at Normal Jump, DashJump including clamps, SkidJump, normal and climbing WallJump, and mantle hop; each consumes storage once. Host suite: **134 passed, 0 failed**. ROM builds; SHA-256 `eee63977eb51acbb3a24d5d58a043faa7ed42315ab7d2ca63806ca2e769caf5b`. Ares untouched. Inc 3 remains open for unified dynamic-actor scheduling, frustum snapshot, and whole-world hitstop.

**Progress update — 2026-10-08:** `ActorWorld` now snapshots active point actors once per ordered pass and dispatches monotonically around external source slots. `GameplayScene` interleaves all baked dynamic-solid slots with point actors and Player; each solid's base velocity displacement calls the rider-aware `MoveDynamicSolidTo` at its own Update slot. LateUpdate reserves same solid slots around Player contact/camera work. `actor_world_source_schedule_smoke` checks both source-ordered passes; full host suite: **136 passed, 0 failed**. ROM build PASS, size **1,900,544 bytes**, SHA-256 `1d4e982084037ae4ac23586db6381e400350a0dd9a94d2354d993de9428e2981`. No Ares launch or interaction. Cassette remains unscheduled; dynamic class routines, pre-pass frustum/offscreen policy, and moving-solid hitstop proof remain open. Auto-grab code untouched.

**Progress update — 2026-10-08:** added OG-shaped actor-update broadphase snapshot, initially using 4:3; source audit below corrects it to the actual 16:9 target. Scene captures after hitstop and before camera input; Update and LateUpdate reuse one immutable view. Point actors provide source LocalBounds without exceeding the 80-byte inline actor slot; dynamic solids test transformed bounds; refill/spring cooldowns opt into offscreen updates. Camera input runs at Player's map-pack source slot. Full host suite at this historical checkpoint: **137 passed, 0 failed**. ROM SHA-256 `39a87bc00e55c58455baf81235367f700255d9f2bd220fa55c5932ec28f37e16`. No Ares interaction.

**Progress update — 2026-10-08:** active-room cassette now gets `source_id` from its mappack spawn record and dispatches through `ActorWorld::DispatchSourceOrderedActorSlot` in Update and LateUpdate, interleaved against dynamic-solid and Player slots. Timer update is separate from Player pickup, preserving source-order cooldown timing; pickup uses OG's feet-anchored waist point and 10-unit radius scaled by 0.2. Cassette keeps its update slot active after collection. Schedule/culling, pickup-radius, and transition tests pass. Full host suite: **138 passed, 0 failed**. ROM build PASS, size **1,900,544 bytes**, SHA-256 `e7505ba576111ad780ce1fdebcc4cbcfa564696dcbe943fb0924edbc81c6b8f3`. No Ares launch or interaction. Cassette remains room-local, and manifest target/persistent collection state are not ported. Dynamic class routines, moving-solid hitstop runtime proof, and exact source-frustum parity remain open; auto-grab untouched.

**Progress update — 2026-10-08:** extended real-map `dynamic_solid_rider_parity_smoke` through world hitstop. Two consumed fixed ticks preserve Player position/timers/carry anchors, dynamic-solid transform/reinsert ordinal, and rider state; next live tick resumes actor movement and climbing-owner carry. Scene contract asserts hitstop's `continue` precedes view capture, input, and source dispatch. Full host suite: **138 passed, 0 failed**. ROM unchanged from cassette build: SHA-256 `e7505ba576111ad780ce1fdebcc4cbcfa564696dcbe943fb0924edbc81c6b8f3`. Ares not launched. Remaining Inc 3 gaps: exact source-frustum derivation/override parity and device validation; cassette remains room-local; dynamic class routines remain for Inc 5-7. Auto-grab untouched.

**Progress update — 2026-10-08:** pinned `Camera` and `Game` source reveal OG target is fixed 640x360 (16:9), not N64's 4:3 output. Corrected broadphase aspect; source projection is vertical 45° × `FOVMultiplier`, near 20 / far 800, then AABB inflation 10. `actor_update_view_source_smoke` pins these source constants and render target; math fixture checks expected 16:9 bounds, speed FOV expansion, and the previously-culled x=550 actor. Full host suite: **139 passed, 0 failed**. ROM build PASS, size **1,900,544 bytes**, SHA-256 `e59b953dbae3347f15424213cc922b947b8adeb7a7c5ac94939dbdf33dd1f064`. No Ares interaction. Source-frustum math is now source-backed for shipped target; map-wide cassette lifetime/target state and device evidence remain open. Dynamic class routines remain for Inc 5-7. Auto-grab untouched.

#### Files to touch

##### `src/user/gameplay/world/actor_world.hpp`, `src/user/gameplay/world/actor_world.cpp`, `src/user/gameplay/world/actor_update_view.hpp`, `src/user/gameplay/world/dynamic_solid_world.hpp`, `src/user/gameplay/world/dynamic_solid_world.cpp`, `src/user/gameplay/actor/actor.hpp`, `src/user/gameplay/actor/refill_actor.*`, `src/user/gameplay/actor/spring_actor.*`, `src/user/gameplay/actor/cassette_actor.*`, `src/user/gameplay/scene/gameplay_scene.cpp`, `tests/actor_update_view_smoke.cpp`, `tests/actor_update_view_source_smoke.py`, `tests/actor_world_source_schedule_smoke.cpp`, `tests/cassette_transition_smoke.cpp`, `tests/dynamic_solid_rider_parity_smoke.cpp`, `tests/run_host_tests.sh`
- Add ordered actor identity records keyed by canonical source entity order; retain heavy dynamic geometry in its separate map-wide registry (capacity >=118), not the 64-slot point-actor storage. Dispatch actor `Update()` callbacks in source order, then `LateUpdate()` callbacks in source order; do not run all dynamic solids before/after all point actors as separate batches.
- Refactor `GameplayScene` tick so it captures offscreen frustum once immediately before pass one, then runs complete Player.Update-equivalent bundle (`TimerInputPhase`, `StatePhase`, `MovePhase`, and player-owned update work) in Player source-order slot; actor `Update()` callbacks interleave by source entity order; then Player.LateUpdate-equivalent contact/ground-snap and actor `LateUpdate()` callbacks run in second ordered pass. Keep input collection/world hitstop at source tick boundaries; camera changes during pass one do not recalculate this tick's captured view.
- Implement source-shaped `MoveTo(target)`: detect each `IRidePlatforms` equivalent against pre-move transformed faces in actor insertion order, publish source velocity, move rider by exact displacement, update actor transform/spatial membership, then restore collision participation.

##### `src/user/gameplay/player/player_controller.cpp`, `src/user/gameplay/player/player_movement.cpp`
- Split `MovePhase` and `LateContactPhase` at an explicit scheduler boundary; preserve all existing per-tick contact accumulation. Reuse `MovingSurfacePhase` / platform carry only where its ordering matches source. Add explicit owner identity to contacts and preserve climb-wall riding.
- Audit every source launch carry call: DashJump restores recorded coyote height, consumes carry once with vertical clamp `[0,180]` and horizontal magnitude cap `300`, then clears storage; compare Normal Jump, SkidJump, both WallJumps, and mantle hop gates to OG `AddPlatformVelocity` sites.

##### `tests/dynamic_solid_rider_parity_smoke.cpp`, `tests/actor_update_view_smoke.cpp`, `tests/actor_world_source_schedule_smoke.cpp`, `tests/refill_render_contract_smoke.py`, `tests/run_host_tests.sh`
- Assert Player-before-TrafficBlock order for current map, Update-before-LateUpdate ordering, point-actor/dynamic-solid interleaving, pre-pass camera/offscreen snapshot behavior, grounded and climbing riders receive displacement once, released/side-contact players do not, DashJump and sibling launch carry gates, and a hitstop tick advances neither actor nor rider.

#### Edge cases
- Zero delta must not reinsert or double-carry.
- A platform moves across the rider's cell seam in one tick.
- Player and multiple actor types interleave in source order; OG first/last rider owner order is observable.
- Collision temporarily disabled during carry must be restored on every exit/error path.

#### Verification
- Run: rider/scheduler/frustum fixtures, existing `surface_parity_smoke` and `move_service_smoke`, full host suite, ROM build.
- Done: per-tick actor/player traces match source actor insertion order, both world passes, and `Solid.MoveTo` oracle for checked cases.

### Inc 4 — Dynamic-solid render cutover (L)
**Depends on:** Inc 1
**Unblocks:** Inc 5, Inc 6
**Done criteria:** all 118 dynamic actors render their exact textured source geometry from actor-local coordinates; static cell meshes contain no duplicate dynamic-solid geometry, and stationary actor output matches its initial source pose.

**Status: implementation and host gates pass; Ares still-frame gate pending — 2026-10-08.**

#### Files to touch

##### `tools/ogworld/model.py`, `tools/ogworld/chunking.py`, `tools/writers/lvl_writer.py`, `tools/writers/dynamic_solid_writer.py`, `tools/dsol_format.py`
- Exclude actor-owned dynamic faces from static LVL cells; emit them once in DSOL actor-local mesh ranges, preserving `Solid.LocalVertices` pivot, source brush/face identity, material ids, UVs, and source face order. Keep stationary-world cell clipping unchanged.

##### `src/user/gameplay/render/`, `src/user/gameplay/scene/gameplay_scene.cpp`
- Load validated DSOL ranges and render each local mesh under current `Solid.Position`/Facing plus source model-only shake; use the same actor transform and source-face ids as collision. Exclude these faces from static-cell submission exactly once.

##### `tests/dynamic_solid_render_smoke.cpp`, `tests/run_host_tests.sh`
- Verify the baked actor vertex/UV/material set survives local-pivot conversion and runtime transform, static cell exclusion, initial-pose equivalence, actor visibility states, and no duplicate draw records across cell seams.

#### Edge cases
- Dynamic brush spans cells; one actor mesh must render once while stationary world geometry remains seam-clipped.
- Material/UV repeat and source texture flags must survive actor-local conversion.
- TShake changes model transform only; collision remains at OG transform.
- Actor hidden, destroyed, or temporarily non-collidable; render and collision flags stay distinct.

#### Verification
- Run: actor render fixture, static cell render contract, full host suite, ROM build.
- Done: host geometry oracle matches actor-local source faces and an Ares still-frame route confirms initial placement/materials without disturbing a live user session.

#### Progress evidence — 2026-10-08
- Dynamic class policy now separates collision from render: all eight classes remain exact CMSH solid proxies for collision, while `RENDER_ACTOR` excludes their faces from static LVL/DLOD cell meshes. DSOL contains each actor-local source face once, in source brush/face order, with material ids and s10.5-safe conditioned UVs.
- Runtime `DynamicSolidRenderer` caches packed local vertices, applies live position/yaw plus render-only `visual_offset`, uses the map material catalog and near-cone culling, and keeps `visible` independent from `collidable`. `OpenWorldRenderer` draws it in the near-pass phase and includes its batches/uploads in existing counters.
- `dynamic_solid_artifact_smoke` checks static-cell exclusion, exact source-face/material/UV identity, actor-local pivot and stationary world transform; `dynamic_solid_render_smoke` loads all current-map records and checks 118 actors / 853 faces / 3,519 vertices, unique face ownership, live transform, visibility/collision independence, and packed UV bounds.
- `./tests/run_host_tests.sh`: **135 passed, 0 failed**. `./compile-rom.sh`: PASS; ROM size 1,933,312 bytes, SHA-256 `b8018de30f0e3b9704883a450ef9521859b845753a283af54db3f70b6260152f`. Targeted `git diff --check` passes. No Ares interaction; user-owned still-frame/material inspection remains the only Inc 4 gate, so Inc 4 is not complete.
- Removed unused `.dlod` wildcard from Makefile's DFS prerequisites; runtime does not load these artifacts, and the stale wildcard referenced a file removed by canonical map-pack regeneration.

### Inc 5 — Current-map `TrafficBlock` runtime (M)
**Depends on:** Inc 1, Inc 2, Inc 3, Inc 4
**Unblocks:** Ares end-to-end platform route
**Done criteria:** all 9 shipped TrafficBlocks wait for a player rider, start after 0.15s, accelerate at 400 to 600 toward their linked Nodes, stop/snap, wait 0.8s, return at 50, wait 0.5s, and keep rendered textured geometry aligned with exact collision.

#### Files to touch

##### `src/user/gameplay/actor/`, `src/user/gameplay/world/actor_factory.cpp`, `src/user/gameplay/world/entity_dispatch.cpp`
- Add an owned TrafficBlock actor with fixed-storage coroutine-equivalent state, transform, velocity, linked target, and Solid owner id. Preserve source tick order: `Solid.Update()` applies prior velocity before `TrafficBlock.Update()` advances its routine; the routine's newly assigned velocity moves the block on following actor update.
- Resolve target from canonical typed MPP5 target lookup records, preserving OG first matching `targetname` and missing-to-zero behavior; do not consume offline `.nav` or its fabricated fallback.

##### `src/user/gameplay/render/`, `src/user/gameplay/scene/gameplay_scene.cpp`
- Render source actor-local faces using source materials/UVs and the actor transform. Exclude that geometry from static-cell render data exactly once.

##### `tests/traffic_block_parity_smoke.cpp`, `tests/real_map_traffic_block_smoke.cpp`
- Compare source-state traces and run the actual current-map path against the actor-owned collision/render pipeline.

#### Edge cases
- Player leaves during travel; source loop continues to endpoint and return.
- Direction reversal/zero distance, variable fixed-step presentation rate, hitstop, and oversized delta.
- Rider at an edge/corner or blocked by static wall; no tunneling or double displacement.
- Player crosses cell boundary while actor remains world-owned.

#### Verification
- Run: source-state host trace, real-map route reaching the Node, render/collision transform checks, full host suite, ROM build.
- Device gate: use Windows Ares only when no user-owned instance is live; require exact ROM SHA, map/render loads, `[physics]` actor/rider telemetry, target reached, and visible actor alignment.
- Done: full phase sequence and route metrics pass; no frozen or duplicate proxy geometry.

### Inc 6 — `FallingBlock` trigger/fall/land/respawn lifecycle (L)
**Depends on:** Inc 1, Inc 2, Inc 3, Inc 4
**Unblocks:** Inc 7
**Done criteria:** all 68 shipped FallingBlocks run source `Wait→Shake→Fall→Landed/Respawn` states that update transform, collision, riders, and visibility; gravity 600 and terminal fall speed -160 match OG; respawn/destroy conditions match submap/group/secret policy.

#### Files to touch

##### `src/user/gameplay/actor/`, `src/user/gameplay/world/actor_factory.cpp`, `src/user/gameplay/scene/gameplay_scene.cpp`
- Add the FallingBlock state machine from `Celeste64-og/Source/Actors/FallingBlock.cs`; preserve trigger/rider entry, shake timing, `EndPosition` derived from the source ray, landing snap, five-second respawn, and destroy conditions.

##### `tools/ogworld/model.py`, `tools/mappack_format.py`, `src/user/gameplay/world/mappack_loader.hpp`, `src/user/gameplay/world/mappack_loader.cpp`
- Serialize `Secret`, `GroupName`/submap flags, and exact fall-end metadata required by runtime; reject missing required geometry/ray results only where source behavior does.

##### `tests/falling_block_parity_smoke.cpp`, `tests/real_map_falling_block_smoke.cpp`
- Cover state boundaries, rider-trigger timing, collision response/landing, visibility, respawn and destruction policy against source oracle.

#### Edge cases
- No lower landing surface; death-plane crossing differs for submap/group/secret blocks.
- Multiple FallingBlocks; deterministic actor and collision order.
- Respawn while player stands on destination; source MoveTo carry applies.
- Hitstop pauses shake, fall, respawn timer, and rendering state transitions.

#### Verification
- Run: focused and real-map FallingBlock routes, full host suite, ROM build; Ares route only when the emulator is not user-owned/live.
- Done: captured source/runtime state sequence and transform error meet exact formula/tick tolerances.

### Inc 7 — Remaining dynamic-solid classes and per-class behavior gates (L)
**Depends on:** Inc 1, Inc 2, Inc 3, Inc 4, Inc 5, Inc 6
**Unblocks:** dynamic-solid portion of full fidelity audit
**Done criteria:** every dynamic class in `class_policy.py` has a source-backed runtime actor, or explicit source-proven non-runtime disposition; no remaining class is silently static-proxy.

#### Files to touch

##### `src/user/gameplay/actor/`, `src/user/gameplay/world/actor_factory.cpp`, `src/user/gameplay/world/entity_dispatch.cpp`
- Port FloatyBlock spring/rider behavior, MovingBlock, GateBlock, CassetteBlock, BreakBlock, and DoubleDashPuzzleBlock states from their OG actor sources; keep each class's activation, collision, animation, reset, and actor-interaction semantics distinct.

##### `tools/ogworld/class_policy.py`, `tools/ogworld/geometry.py`, `tools/ogworld/collision.py`, `docs/og_port_gap_matrix.md`
- Move completed classes from `static-proxy` to actor-owned policy only when runtime/artifact/render paths are live; record remaining source rows and proof status.

##### `tests/`, `tests/run_host_tests.sh`
- Add one source-derived fixture and actual-map route per active dynamic class; add negative tests for unsupported dynamic classes so baking fails closed.

#### Edge cases
- Source classes depend on puzzle flags, cassette timing, break/reform, groups, hidden/transparent state, or actor destruction.
- A state can be stationary but still collidable/non-collidable or visible/invisible.
- Actor overflow, missing required component, malformed target, and map-cell transition.

#### Verification
- Run: every focused actor test, current-map real-map route, full host suite, ROM build, and device telemetry/performance gates.
- Done: dynamic rows in `docs/og_port_gap_matrix.md` are source-backed and tested; overall goal still remains open for other physics/model gaps.

## Cross-cutting verification

- Preserve MPP2-4 read compatibility; ensure MPP5 bake/load hash and size identity.
- Run `./tests/run_host_tests.sh` after each increment and `./compile-rom.sh` after N64 runtime edits.
- Compare source-derived fixed-tick traces for actor state, transform, velocity, collision hits, rider deltas, and visibility.
- Run current-map route tests with actual CMSH faces and no synthetic grounded flags.
- Before every Ares capture, inspect for an already-running instance; never close or replace the user-controlled emulator. Device proof must bind telemetry to the exact ROM SHA and show actor state plus collision/rider evidence.
- Measure ROM/RDRAM and actor-query cost on actual N64/Ares workloads; keep exact behavior, not a faster approximation.

## Standards / common-mistakes referenced

- `.agents/common-mistakes/og-face-identity-must-survive-bvh.md` — preserve face/solid identity and disclose that source spatial ordering is query-dependent.
- `.agents/common-mistakes/sweep-contact-flags-must-accumulate.md` — preserve contacts across actor-driven sweeps.
- `.agents/common-mistakes/dashjump-launch-parity.md` — platform velocity storage/consumption gates.
- `.agents/learnings/og-single-mover-port.md` — one ordered resolver, world-tick hitstop, and movement/collision source formulas.
- `.agents/standards/index.yml` and `.agents/common-mistakes/index.yml` are absent in this checkout.

## Open questions (CONSIDER from review)

- Final Ares visual/device acceptance depends on user-owned emulator availability; keep source/math and host tests independently runnable, and never close or replace an interactive session.
- GateBlock and CassetteBlock have zero instances in current `1.map`; Inc 7 must validate their real source behavior with synthetic fixtures and ensure registry/factory coverage, not claim current-map route coverage.

## Out of scope

- This plan does not close Feather/Bubble/Death/Respawn player-state gaps, all NPC behavior, or pixel-perfect player/hair/lighting parity; those remain under `.plans/full-og-fidelity/plan.md`.
