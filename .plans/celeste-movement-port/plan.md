# OG Celeste Movement and Physics Port

## Context

The climb bug remains after several substantial passes. The repository already tried wall-climb physics, climb-and-zoom fixes, a wall-climb overhaul, a method-by-method climb parity pass, a controls/physics clean port, and an OG single-mover overhaul. Those efforts fixed real issues, but completion was often inferred from host fixtures or partial scripted routes while the actual Ares/input behavior remained unverified. A later delete-first rebuild and a large restore-parity patch also show that another broad reset would repeat the uncertainty rather than isolate the defect.

The next effort is a source-mapped port of OG Celeste64's player movement, collision response, climb, and control-state behavior from `Celeste64-og/Source/Actors/Player.cs` and `Solid.cs` into the N64 C++ gameplay layer. The goal is to make every intentional difference explicit, find and fix the current climb failure using a reproducible real-map case, and prevent fixes in input, state transitions, collision geometry, or movement from silently compensating for one another.

The plan preserves N64-specific boundaries: OG uses +Z up and feet-anchored positions; the demake uses +Y up and a center-anchored `PlayerState`. The N64 controller mapping, 60 Hz fixed-step loop, collision mesh, and renderer remain platform adapters. They must not change OG gameplay semantics without a named and verified adaptation.

The source call order matters: `StClimbingUpdate` can issue a kinematic `SweepTestMove(move * ClimbSpeed * dt, false)` at `Player.cs:1515`; after `stateMachine.Update()`, `Player.Update()` also applies `velocity * dt` through `SweepTestMove` and then calls one final `Popout(false)` (`Player.cs:341-373`). The port must preserve these ordered movement requests and one shared resolver implementation; “one mover” does not mean collapsing climb and velocity movement into one displacement or clearing contacts between the requests.

## Architectural decisions

- Decision: Port behavior and ownership method-by-method, not C# class structure. Rationale: OG's `Player` combines state and movement while the C++ project intentionally separates input, controller, mover, world queries, and collision mesh.
- Decision: Keep `InputSystem` as the N64 device adapter and port OG's gameplay input semantics behind it. Rationale: OG keyboard/GameCube bindings do not map directly to N64; raw Z/L/R, stick axes, button edges, and tick consumption must remain observable at the boundary.
- Decision: Preserve one authoritative movement resolver implementation for normal, dash, and climb movement, while retaining OG's ordered per-state invocations. A climb tick submits a kinematic displacement during state update, followed by the ordinary `velocity * dt` sweep and final `Popout(false)`. N64-specific finite-face query behavior is explicit in the semantic query layer and has synthetic plus baked-map fixtures.
- Decision: Keep all collision response in the shared `MoveService`; retain only measured query/response adaptations for bounded N64 mesh faces, with a regression fixture for each adaptation.
- Decision: Establish one fixed-tick order and one owner for each input edge, movement state, contact result, and position/velocity write. Rationale: the prior port dropped timer writers, consumed launch state in the wrong tick, and mixed pre/post-move contact ownership.
- Decision: Diagnose current runtime behavior before changing mechanics. Every runtime result must identify the exact ROM SHA/build, successful map/render boot, raw input bits, and the state/query events that led to climb entry or exit.
- Decision: Keep increments small and independently reviewable. Do not reset or overwrite the current working tree; it contains extensive user changes and should be re-inspected before implementation.

## Assumptions and answers from code

- Answered from code: the current gameplay loop is split into `TimerInputPhase`, `StatePhase`, and `MovePhase` at `src/user/gameplay/player/player_controller.cpp:527-532`; `GameplayScene` drives those phases once per fixed tick at `src/user/gameplay/scene/gameplay_scene.cpp:1397-1426`.
- Answered from code: normal, dash, platform carry, and climb displacement requests now route through the same `MoveService::SweepMove`/`Popout` implementation. Climb retains OG's separate ordered kinematic request before the ordinary velocity request and final popout.
- Answered from code: OG `Player.Update()` runs the state machine, then sweeps `velocity * dt`, then calls one final `Popout(false)` at `Celeste64-og/Source/Actors/Player.cs:341-373`. `SweepTestMove`/`Popout` are at lines 642-704. `StClimbingUpdate` also performs a kinematic `SweepTestMove(..., false)` at line 1515. Preserve the ordered multiple calls through one resolver; refresh source line ranges against the checked-out reference before each increment.
- Answered from code: OG is +Z-up and feet-anchored; the demake is +Y-up and center-anchored. All vertical probes, wall anchors, ground pushout, mantle height, and launch restoration therefore require explicit conversion.
- Answered from code: the device runbook requires raw `[input] z=0 l=0 r=0` at rest before interpreting uncommanded grabs. It also requires identifying the Ares Port 1 mapping and checking `[climb] START` raw bits. A prior capture observed constant Z asserted at rest, so input remains a first-class hypothesis.
- Answered from code: expected project checks are `./tests/run_host_tests.sh` (or `make test`) and `./compile-rom.sh`; device acceptance uses Ares and the physics capture tools documented in `AGENTS.md`.
- Assumption: target behavior is OG Celeste64 movement/climb parity, with only necessary N64 input, coordinate, and collision-mesh adaptations. Source: user request; the existing ROM and controller constraints are in `AGENTS.md`.
- Assumption: camera presentation is outside the movement port except where camera-relative input direction or wall-relative vectors affect gameplay. Source: default; the requested scope is player movement, physics, and controls.
- Assumption: current uncommitted edits, including the input debounce work, are user-owned in-progress state. Each implementation increment must inspect and preserve them rather than reset the worktree or treat them as accepted fixes.

## Risks accepted

- The current climb symptom may be caused by a held/mis-mapped shoulder rather than physics. Inc 1 separates these with raw idle and press/release evidence before selecting a code change.
- OG and the baked CMSH do not share exact geometry/query behavior. Coordinate conversion, face winding, finite face extents, and radius coupling need explicit contracts; do not tune independent radii until zero-popout equilibrium is measured.
- Reusing the full mover for climb may reintroduce the previously observed slope ejection. The plan accepts that this may require one explicit in-resolver collision policy; it does not accept an untested parallel resolver.
- Deterministic host fixtures cannot establish controller feel or Ares behavior. Each behavior slice must pass host tests and its applicable real-map/device route before being called complete.
- Ares may be unavailable in some sessions. In that case, ROM builds and host evidence are useful but the device acceptance increment stays open; they do not substitute for a raw-input capture or manual playtest.
- The repository worktree is heavily modified. Implementation must not use blanket checkout/reset or stage unrelated user changes.

## Increment DAG

### Execution record — 2026-10-07

- The user directed continuing the plan through implementation and fixing the reported trigger/climb stalls. The source-mapped movement, shared mover, and climb increments are implemented; remaining parity limits are recorded in `docs/og_port_gap_matrix.md`.
- Ares is installed on **Windows** at `C:/Users/samue/Documents/ares-v147/ares.exe`; WSL builds the ROM and Ares reads its UNC path. The earlier Linux-only availability finding is superseded.
- SHA `cd11b81addb8e72658db62b18f82d245e3c27740dafe24dfdf1e9ea6092486c8`, build `Oct 7 2026 00:03:54`, booted the real map, collision and renderer in Windows Ares. Raw Z was asserted; the user confirmed pressing the trigger released climb. The VirtualPad1 R-Trigger binding was inverted (`axis 2/Lo`). A backed-up `Lo` → `Hi` correction produced raw `z=0 l=0 r=0` at rest. Captures are in `evidence/windows-trigger-inverted.log` and `evidence/windows-trigger-corrected.log`.
- The synchronous 60-line climb-entry trace was removed from normal gameplay; diagnostic builds keep a bounded in-memory window and drain at most one sample per tick. The per-tick full-CMSH `LedgeHopMiss` scan was removed; climb retains bounded wall-face guards.
- Inc 1–8 implementation and deterministic/device checks are complete. The host suite passes 86/86; the normal ROM SHA is `e9beb7e9f7ef3a595d968121ed3875cda4bf739a5edbf92128603a3e0d1495e5`. Final Windows Ares walljump, mantle, and corner profiles each pass their own route gate with zero idle shoulder bits. Manual controller feel remains for user acceptance; autoplay cannot certify it.

- Inc 1 — Reproduce and instrument the current climb failure (M) — depends on: none — unblocks: 3
- Inc 2 — OG-to-C++ behavior map and reference fixtures (M) — depends on: none — unblocks: 3, 4
- Inc 3 — Input sampling and player tick/state contract (M) — depends on: 1, 2 — unblocks: 5, 6, 7
- Inc 4 — Coordinate, contact, and collision-query contract (M) — depends on: 2 — unblocks: 5, 6, 7
- Inc 5 — Port the OG mover and Popout call-order contract (L) — depends on: 3, 4 — unblocks: 6, 7
- Inc 6 — Port normal, air, jump, dash, and launch state behavior (L) — depends on: 3, 4, 5 — unblocks: 8
- Inc 7 — Port climb entry, sustain, cornering, and mantle behavior (L) — depends on: 3, 4, 5 — unblocks: 8
- Inc 8 — Integrate climb wall-jump and ROM acceptance; close parity gaps (M) — depends on: 6, 7 — unblocks: completion

## Increments

### Inc 1 — Reproduce and instrument the current climb failure (M)

**Status:** done — Windows captures bind raw Z inversion and corrected idle shoulders to exact ROM/build; full suite passes. Final manual feel remains in Inc 8.

**Depends on:** none  
**Unblocks:** 3  
**Done criteria:** a fresh, SHA-identified ROM either reproduces the failure with raw inputs and the climb transition sequence captured, or documents a controlled no-reproduction run plus the exact manual scenario needed to reproduce it.

#### Files to touch

##### `src/user/gameplay/rom_telemetry.cpp`, `src/user/gameplay/rom_telemetry.hpp`, `src/user/gameplay/player/player_controller.cpp`, `src/user/gameplay/world/world.hpp`, `src/user/gameplay/world/world.cpp`
- What changes: add or tighten bounded diagnostic events for climb input, entry checks, selected face/normal, anchor, movement delta, Popout correction, state transition, and release reason.
- Function(s): extend existing telemetry only; do not change physics or input acceptance in this increment.
- Data shapes: write diagnostic records into a fixed-size in-memory ring buffer (for example, 60 ticks before and 30 after a climb transition/anomaly) with raw `z/l/r`, movement intent, state before/after, query result/face id, position before/after, velocity, and correction vector. Flush only on the bounded event window, not continuously to serial. Instrument the existing `World` query result as it returns to the controller; do not issue a second query to produce telemetry. Keep normal periodic telemetry unchanged.
- Integration points: `InputSystem` raw sample -> `GameplayScene` fixed tick -> `PlayerController` climb gates -> `World` semantic query -> `MoveService` resolver.
- Error paths: missing room/query results are explicit misses; telemetry must not dereference a null room or affect gameplay timing.

##### `tools/capture_baseline.sh`, `tools/capture_physics_autoplay.sh`, `tests/real_map_guided_climb_smoke.cpp`
- What changes: ensure the capture identifies ROM SHA/build, successful map/collision/render startup, zero raw shoulders at rest, and a real approach/contact/grab/sustain/exit route; do not count a teleport-to-wall as climb entry proof.
- Function(s): extend verifier route gates only where current output lacks the required evidence.
- Data shapes: capture header plus ordered climb event trace; terminal PASS is profile-specific and requires the expected entry and exit events.
- Integration points: Ares stdout and real baked Forsaken City CMSH.
- Error paths: stale marker, SHA mismatch, missing render/map evidence, held raw shoulder at rest, or absent route event yields an invalid capture, not a physics diagnosis.

##### `docs/physics_feel_acceptance.md`
- What changes: record one exact repro recipe, ROM SHA, controller mapping, raw input idle/press/release observations, wall geometry, facing/move input, and event trace.

#### Edge cases

- If `[input] z/l/r` is nonzero while untouched, classify the result as input/mapping evidence and stop before changing physics.
- If raw bits are zero but climb starts, preserve the query and state-transition trace; do not add another speculative grab gate.
- If no Ares session is available, leave device reproduction explicitly unverified.

#### Verification

- Run: `./compile-rom.sh`; launch the identified ROM in Ares using the repository runbook.
- Tests to add/update: capture verifier checks required fields and rejects stale/missing artifact, input, and route evidence.
- Done: exact artifact and raw-input evidence proves or narrows the user's reported failure.

### Inc 2 — OG-to-C++ behavior map and reference fixtures (M)

**Status:** done — source matrix and source-derived fixtures verified by the full host suite.

**Depends on:** none  
**Unblocks:** 3, 4  
**Done criteria:** every in-scope OG update, input, collision, and climb operation maps to one C++ owner, one tick position, one coordinate conversion, and a named parity fixture or documented N64 exception. Source mapping and general fixtures can proceed while Inc 1 captures the current failure; add a failure-specific fixture after that trace exists.

#### Files to touch

##### `docs/og_port_gap_matrix.md`, `.agents/learnings/og-single-mover-port.md`
- What changes: replace broad “done” claims with a method-by-method matrix for `Player.Update`, state handlers, `TryClimb`, `ClimbCheckAt`, `SweepTestMove`, `Popout`, `GroundCheck`, corner/ledge logic, and input sampling. Correct the learning note's “one call per tick” wording to say there is one shared resolver implementation with multiple ordered call sites during climb plus the final `Popout(false)`.
- Data shapes: columns for OG source/call order, C++ owner/call order, axes/anchor convention, parity status (`matched`, `intentional N64 adaptation`, `unproven`, `divergent`), source evidence, and test/capture.
- Error paths: unverified behavior remains unverified; constants that happen to match do not count as parity.

##### `tests/movement_parity_smoke.cpp`, `tests/climb_parity_smoke.cpp`, `tests/physics_query_regression.cpp` (or focused new files)
- What changes: add fixture cases from OG call order and representative expected values, not from current C++ output.
- Function(s): keep fixtures host-runnable and independent of libdragon.
- Data shapes: input state + prior `PlayerState` + collision fixture -> expected next state/contact/event trace.
- Integration points: fixtures target `PlayerController`, `MoveService`, and `World` query contracts at their public test seams.
- Error paths: fixture setup must provide a real support plane when testing grounded behavior and valid room geometry when testing rays.

##### `Celeste64-og/Source/Actors/Player.cs`, `Celeste64-og/Source/Actors/Solid.cs`
- What changes: reference only; verify source line ranges and preserve the relevant callsite excerpts/links in the matrix. Do not port unrelated camera/render/actor behavior.

#### Edge cases

- Record every feet-to-center and +Z-to-+Y conversion explicitly.
- Separate analog magnitude behavior on ground from normalized airborne steering; separate grounded and airborne dash launch vectors.
- Preserve OG transition and movement-call ordering, including synchronous state enter/exit behavior, all writers of cooldown/buffer timers, climb kinematic movement inside the climbing update, outer velocity sweep, and final idle Popout.

#### Verification

- Run: `./tests/run_host_tests.sh`.
- Tests to add/update: assert the matrix's critical divergences with at least one fixture per state family.
- Done: no “parity” row lacks a source callsite and an observable proof.

### Inc 3 — Input sampling and player tick/state contract (M)

**Status:** done — edge latches, simulation buffers, fresh snapshots, immediate climb and single-handler transitions verified; full suite/build pass.

**Depends on:** 1, 2  
**Unblocks:** 5, 6, 7  
**Done criteria:** held, pressed, and released inputs are sampled and consumed exactly once at the documented fixed tick; climb remains unbuffered like OG unless a device-specific filter is separately justified and measurable.

#### Files to touch

- What changes: define the N64-to-gameplay adapter contract for stick, held buttons, press/release edges, and raw shoulders.
- Data shapes: one immutable `PlayerInput` snapshot per simulation tick, separating raw device values from gameplay deadzone/edge state. Pressed/released edges sampled when there are zero simulation ticks remain latched for the next tick; during catch-up, held values apply on every tick while each edge is delivered only on the first tick, then consumed once.
- Integration points: controller polling occurs once per rendered frame; `GameplayScene` consumes/latches edges across fixed simulation ticks without double-consuming or inventing presses.
- Error paths: multiple catch-up ticks, no simulation tick, dropped frame time, press and release both observed by separate polls before the next tick, and controller disconnect must not lose an observed edge, synthesize climb-held, or repeat a one-shot action. A press and release occurring entirely between hardware polls cannot be recognized and is outside the guarantee.

##### `src/user/gameplay/player/player_state.hpp`, `src/user/gameplay/player/player_controller.hpp`, `src/user/gameplay/player/player_controller.cpp`, `src/user/gameplay/scene/gameplay_scene.cpp`
- What changes: specify one owner for timers, synchronous transitions, and state enter/exit actions; port OG's handler order without pre-FSM cascades.
- Function(s): audit `TimerInputPhase`, `StatePhase`, `StNormalUpdate`, `StSkiddingUpdate`, `StDashingUpdate`, `StClimbingUpdate`, and state-entry helpers.
- Data shapes: `PlayerState` holds authoritative state/velocity/contact/timer values; input snapshot is read-only for the tick.
- Integration points: `Scene` orders input -> timers -> one state handler that produces ordered movement intent -> `MovePhase` applies any climb kinematic sweep, then the ordinary velocity sweep, then the final idle `Popout(false)` and late contacts, mapped against OG `Update`/`LateUpdate`.
- Error paths: transitions during a handler cannot run a second state's update in the same tick unless OG does; every gated timer must have an enumerated setter and decay site.

##### `tests/input_compat_smoke.cpp`, `tests/movement_parity_smoke.cpp`, `tests/run_host_tests.sh`
- What changes: assert input edge lifetime across 0/1/N fixed ticks (zero-tick latch, first catch-up tick only), held shoulder behavior across all catch-up ticks, press and release before the next tick, deadzone and move-axis mapping, transition call count, and timer writer coverage.

#### Edge cases

- The current worktree has an uncommitted two-poll climb debounce. Measure it against the OG's unbuffered climb hold and Inc 1 raw-input trace; do not silently preserve or remove it as a presumed fix.
- Device analog triggers remain outside N64 Z/L/R unless there is explicit hardware evidence and user-approved mapping change.

#### Verification

- Run: `./tests/run_host_tests.sh`; inspect `[input]` and `[climb]` telemetry on hardware/emulator.
- Done: no duplicate input edges, hidden debounce, or same-tick state cascade remains undocumented.

### Inc 4 — Coordinate, contact, and collision-query contract (M)

**Status:** done — finite slices, normals, polygon fan selection, positional consumers and synthetic/baked fixtures verified; full suite/build pass.

**Depends on:** 2  
**Unblocks:** 5, 6, 7  
**Done criteria:** OG-style probes return the same semantic contact for bounded fixtures independent of mesh winding, with explicit conversions between OG axes/anchors and C++ axes/anchors.

#### Files to touch

##### `src/user/gameplay/math_types.hpp`, `src/user/gameplay/physics/geom.hpp`, `src/user/gameplay/physics/geom.cpp`, `src/user/gameplay/physics/coll_mesh.hpp`, `src/user/gameplay/physics/coll_mesh.cpp`
- What changes: define query semantics, contact normal orientation, finite-face bounds, sweep hit ordering, and center/feet adapter helpers without adding another repair path.
- Data shapes: query result contains hit/miss, point, normal oriented toward the player, stable face/material id, and distance; moving body shape and anchor convention are named inputs.
- Integration points: `World` semantic queries call the collision mesh primitives and convert to gameplay coordinates in one documented boundary.
- Error paths: reversed winding, degenerate faces, exact tangency, wall top/lip, one-way surfaces, disconnected faces, and null room are bounded and deterministic.

##### `src/user/gameplay/world/world.hpp`, `src/user/gameplay/world/world.cpp`, `src/user/gameplay/player/movement_config.hpp`
- What changes: expose semantic `GroundCheck`, `ClimbCheckAt`, `ClimbNormalCheck`, and wall queries with the OG probe origin/range and N64 conversion applied once.
- Function(s): document/update `QueryWall*`, `ClimbCheckAt`, ground/ceiling ray helpers, and body/climb pushout constants.
- Data shapes: independent named constants for OG pushout, query reach, body support, and rendered-model clearance; no accidental aliasing between them.
- Error paths: plane hits outside a face's vertex extent are misses; body radius and climb query reach are tested together for zero-popout equilibrium.

##### `tests/physics_query_regression.cpp`, `tests/climb_parity_smoke.cpp`, `tests/real_map_inner_corner_clearance_smoke.cpp`
- What changes: cover reversed faces, bounded lips, one-way pass-through, exact tangency, center/feet conversion, normals, and the actual baked CMSH.

#### Edge cases

- Preserve OG's wall/ground/ceiling response order and independent wall popout while grounded.
- Keep geometry search radius, climb query reach, body resting radius, and model-render gap distinct; couple only where a tested equilibrium requires it.

#### Verification

- Run: `./tests/run_host_tests.sh`.
- Done: each query's semantic contract has fixtures on both synthetic geometry and the real baked map.

### Inc 5 — Port the OG mover and Popout call-order contract (L)

**Status:** done — shared ordered sweeps, reset-before-carry, slope fixtures, and the coupled real-map corner route pass; latest host suite is 86/86. Final walljump, mantle, and corner Windows profiles pass (recorded in `docs/physics_feel_acceptance.md`).

**Depends on:** 3, 4  
**Unblocks:** 6, 7  
**Done criteria:** one shared OG-shaped sweep/popout implementation preserves each state's ordered movement requests; normal/dash traces prove the ordinary velocity sweep and final Popout, while climb proves the kinematic sweep, ordinary velocity sweep, and final Popout occur in OG order with contact flags accumulated across the full tick.

#### Files to touch

##### `src/user/gameplay/player/player_movement.hpp`, `src/user/gameplay/player/player_movement.cpp`
- What changes: converge movement on `MoveService::SweepMove`/`Popout` with the same substep and resolve-impact semantics as OG `SweepTestMove`/`Popout`; remove the separate `SweepMoveClimb` implementation after parity fixtures identify the smallest safe response policy.
- Function(s): keep `MoveService::SweepMove(PlayerState&, const Room&, const Vec3& amount, bool resolve_impact) const` for each ordered displacement. Add a begin-of-tick contact reset and keep `Popout(PlayerState&, const Room&, bool resolve_impact) const` as the shared repair primitive.
- Data shapes: `PlayerMoveIntent` carries an optional climb kinematic displacement from `StatePhase`; per-tick contacts are reset once before any movement request and accumulate across every substep and sweep.
- Integration points: `StatePhase` returns/sets the ordered kinematic intent; `MovePhase` applies `SweepMove(kinematic_delta, false)` when present, then `SweepMove(velocity * dt, tNoMove <= 0)`, then exactly one final `Popout(false)`, followed by ground-snap/late-contact work. Climb's kinematic sweep is a state-update movement request, not an alternate algorithm.
- Error paths: a zero displacement makes `SweepMove` return without a hidden Popout; `MovePhase` still runs the single final idle Popout. Contact flags are not cleared between kinematic and velocity sweeps. Wall test is not hidden behind floor/ceiling `else`; one-way floor/ceiling semantics match OG; only the first wall hit resolves impact where OG does so.

##### `src/user/gameplay/player/player_controller.hpp`, `src/user/gameplay/player/player_controller.cpp`, `src/user/gameplay/player/player_state.hpp`, `src/user/gameplay/scene/gameplay_scene.cpp`
- What changes: have `StClimbingUpdate` produce its wall-tangent kinematic displacement as `PlayerMoveIntent`; pass that intent from `GameplayScene` into `MovePhase`, which then performs both OG-ordered sweeps and the final Popout. Remove direct position correction and parallel climb resolver calls.

##### `tests/movement_contracts_test.cpp`, `tests/climb_no_clip_smoke.cpp`, `tests/climb_corner_smoke.cpp`, `tests/climb_mantle_smoke.cpp`, `tests/real_map_guided_climb_smoke.cpp`
- What changes: assert exact call order for normal/dash and climb (kinematic sweep -> velocity sweep -> final Popout), one contact reset per fixed tick, accumulated flags across both sweeps, per-tick popped-out invariant, no wall-normal velocity pump, zero-motion stability, climb slope behavior, and real-map face retention.

#### Edge cases

- A prior climb-only waist resolver avoided repeated head/ceiling ejection on sloped walls. Compare before/after per-tick correction vectors and OG geometry behavior; if this is a true CMSH representation mismatch, add an explicit response policy inside the common resolver and document the exact failing fixture.
- Do not let the renderer's model gap or static model bounds alter the physics body's collision/query shell.

#### Verification

- Run: `./tests/run_host_tests.sh` and `./compile-rom.sh`.
- Device: use route telemetry to compare ordinary wall approach, vertical climb, sloped climb, and release.
- Done: no independent climb sweep implementation remains, unless Inc 2/4 evidence documents a collision-format exception implemented as a policy within the same resolver; OG's multiple sweep call sites and final Popout remain in the mapped order.

### Inc 6 — Port normal, air, jump, dash, and launch state behavior (L)

**Status:** done — source-ordered normal/skid/climb/dash/jump handling, pre-dispatch timers, whole-world dash hitstop, collision-reduced dash expiry, spring center/feet conversion, coyote boundary, and support carry pass the 86-test host suite; normal ROM SHA `e9beb7e9f7ef3a595d968121ed3875cda4bf739a5edbf92128603a3e0d1495e5`. Final Windows profiles pass; manual feel is tracked separately in Inc 8.

**Depends on:** 3, 4, 5  
**Unblocks:** 8  
**Done criteria:** source-mapped deterministic fixtures cover each OG normal/air/dash transition and launch/exit vector, including update ordering and coyote/platform carry.

#### Files to touch

##### `src/user/gameplay/player/player_controller.cpp`, `src/user/gameplay/player/player_state.hpp`, `src/user/gameplay/player/movement_config.hpp`
- What changes: reconcile `Normal`, `Skidding`, and `Dashing` update handlers to OG formula/order, explicitly labeling only hardware-driven differences.
- Function(s): normal ground/air movement, `Jump`, `WallJump`, `SetDashSpeed`, dash steering/expiry, `DashJump`, coyote and platform-velocity consumers.
- Data shapes: state transitions consume immutable tick input; launch helpers return/write a full 3D vector and consume stored carry exactly once; timers have explicit start, decrement, and clear owners.
- Integration points: `PlayerController` computes intent/velocity; `MoveService` owns spatial movement and contact repair.
- Error paths: partial stick differs on ground vs air; no-input air friction is applied; wall-jump launch is not attenuated by same-tick friction; dash hitstop does not consume active duration; grounded and airborne dash vectors remain distinct.

##### `tests/movement_parity_smoke.cpp`, `tests/movement_contracts_test.cpp`, `tests/real_map_slope_physics_smoke.cpp` (new)
- What changes: assert OG-derived one-tick vectors and state transitions for partial stick, no-input air decay, overspeed steering, coyote jump, dash buffer/hitstop/exit, and DashJump coyote-height restore. Include a moving-support fixture proving carry is populated before launch and consumed exactly once.

#### Edge cases

- Check both readers and writers for every buffer/cooldown; sustained climb and every OG takeoff site must refresh ground-snap cancellation.
- A passing velocity-only fixture is insufficient if `MovePhase` is skipped; integration fixtures must execute the real tick sequence and support geometry.

#### Verification

- Run: `./tests/run_host_tests.sh`; build ROM.
- Done: all movement formulas and state writes are classified in the gap matrix as proven or explicit N64 adaptation.

### Inc 7 — Port climb entry, sustain, cornering, and mantle behavior (L)

**Status:** done — source-ordered entry/sustain/corner/mantle host routes pass; final real-map Windows walljump, mantle, and corner profiles qualify. Manual controller feel is tracked separately in Inc 8.

**Depends on:** 3, 4, 5  
**Unblocks:** 8  
**Done criteria:** real-map routes prove entry, stable grip, vertical/side movement, corner handoff, mantle, and release against OG call order with raw inputs recorded.

#### Files to touch

##### `src/user/gameplay/player/player_controller.cpp`, `src/user/gameplay/player/player_state.hpp`, `src/user/gameplay/world/world.cpp`, `src/user/gameplay/world/world.hpp`
- What changes: port OG `TryClimb`, `ClimbCheckAt`, `ClimbNormalCheck`, `StClimbingUpdate`, inner/slope-away/outer-corner checks, ledge hop, and release in source order. Keep the existing wall-jump call contract intact; Inc 8 validates it against the Inc 6 implementation.
- Function(s): retain clear named helpers for each OG operation; remove undocumented grace timers, alternate gates, counters, and candidate-selection heuristics unless Inc 1/2 proves an N64-specific need.
- Data shapes: climb state records the OG-equivalent active wall/grip, any corner destination/ease state, and only timers present in OG or explicit adaptations; wall identity/normal provenance is observable.
- Integration points: semantic world queries provide bounded face results; controller asks the common mover for one kinematic displacement; input uses raw gameplay hold/release semantics; telemetry records transition reason.
- Error paths: null room, reversed face winding, finite wall lips, unsupported edges, disconnected corner candidate, explicit release during ease, exact reach tangency, and grounded climb entry.

##### `tests/climb_bug_probe.cpp`, `tests/climb_parity_smoke.cpp`, `tests/climb_mantle_smoke.cpp`, `tests/climb_corner_smoke.cpp`, `tests/real_map_guided_climb_smoke.cpp`, `tests/real_map_corner_traverse_smoke.cpp`
- What changes: update/delete fixtures based on the source map; require 30-tick zero-popout equilibrium, no regrab pump, bounded movement, one corner event, destination retention, real lip mantle, and exit reasons.

#### Edge cases

- Entry visibility radius and sustain reach are different OG operations; body pushout and probe reach must be tested together.
- Plane queries are infinite unless bounded by real face geometry; do not “fix” mantle with counters that remain reset by an unbounded plane hit.
- During corner ease, keep the adopted destination and ease origin; skip normal grip/facing/loss gates until completion but still honor explicit button release.
- Keep model clearance against both wall planes during corner easing, outside physics state.

#### Verification

- Run: `./tests/run_host_tests.sh` and `./compile-rom.sh`.
- Device: `tools/capture_physics_autoplay.sh` profiles must cover climb/mantle/corner independently; interactive Ares play validates controller feel with `[input] z=l=r=0` at rest.
- Done: every climb route has exact artifact identity, raw input, real-map approach, expected events, and per-metric route gates; separate scripted and manual acceptance are both recorded.

### Inc 8 — Integrate climb wall-jump and ROM acceptance; close parity gaps (M)

**Status:** implementation, 86 host tests, final ROM build, and three isolated Windows Ares route captures pass. Manual controller feel remains pending user acceptance; do not mark the plan fully complete until that is recorded.

**Depends on:** 6, 7  
**Unblocks:** completion  
**Done criteria:** a fresh ROM passes automated runtime-health/route gates and a human can reproduce the recorded climb scenarios without spontaneous grab, popout pumping, clipping, or unexplained state changes.

#### Files to touch

##### `src/user/gameplay/player/player_controller.cpp`, `tests/real_map_walljump_smoke.cpp` (new), `docs/physics_feel_acceptance.md`, `docs/og_port_gap_matrix.md`, `docs/movement_spec.md`, `AGENTS.md`
- What changes: verify climb wall-jump invokes the shared Inc 6 launch helper with OG ordering and launch velocity, then update claims from captures and source-mapped results only; record exact accepted ROM SHA, controller mapping, known intentional N64 deviations, and any unresolved gap.
- Tests: add/update a real-map route assertion that enters climb, performs wall-jump, and checks the launch event/vector and release transition.
- Integration points: final docs consume host, capture, and manual-play evidence; don't declare a full port complete while any critical row remains unverified.

##### `tools/capture_physics_autoplay.sh`, `tools/capture_baseline.sh`
- What changes: retain independent climb, mantle, corner, and non-climb profiles; qualify captures with startup/render/input/route health and exact ROM SHA.
- Error paths: Ares unavailable or manual input unavailable leaves the corresponding acceptance incomplete rather than converting to a host-only pass.

#### Edge cases

- Verify the actual launch target is the newly built ROM, not an older file selected in Ares.
- Validate each profile's own terminal event; a mantle success cannot satisfy corner or wall-jump coverage.
- Do not infer manual feel from autoplay telemetry.

#### Verification

- Run: `./tests/run_host_tests.sh`, `./compile-rom.sh`, then a fresh Ares ROM session and required capture profiles.
- Manual: repeat the Inc 1 reproduction recipe and ordinary approach/climb/release controls; compare expected state events and confirm zero raw climb shoulders while idle.
- Done: no unexplained climb transition remains, all critical parity rows are proven or explicitly documented as N64 adaptations, and the ROM identity is archived with the evidence.

## Cross-cutting verification

- Each increment runs `./tests/run_host_tests.sh`; N64-facing movement changes also run `./compile-rom.sh`.
- Every scripted capture binds its telemetry to exact ROM SHA/build, successful map/collision/catalog/render startup, zero raw shoulder values at idle, an actual route, and profile-specific metrics.
- Keep raw per-tick state/input/query/contact traces for the first reproducer and compare state, position, velocity, and contact transitions—not just aggregate route distance.
- Validate movement and climb on synthetic fixtures and the real Forsaken City baked CMSH; neither alone is sufficient.
- Human Ares validation remains separate from scripted physics validation.

## Standards / common-mistakes referenced

- `AGENTS.md` — build, controller mapping, raw Z/L/R diagnosis, ROM identity, Ares and capture runbook.
- `.agents/learnings/og-single-mover-port.md` — one shared mover implementation, ordered same-tick call sites, Popout ordering, zero-motion repair, feet/center conversion, and avoid re-porting validated math.
- `.agents/learnings/climb-probe-shell-geometry-coupling.md` — coupled probe/body radii, bounded infinite-plane hits, corner ray direction, and zero-popout equilibrium.
- `.agents/common-mistakes/climb-ease-must-keep-destination.md` — corner ease ownership and real baked-edge proof.
- `.agents/common-mistakes/sweep-contact-flags-must-accumulate.md` — contacts accumulate over all movement substeps.
- `.agents/common-mistakes/takeoff-needs-ground-snap-cancel.md` — audit cooldown readers and all OG writers, including sustained climb.
- `.agents/common-mistakes/air-movement-parity.md` — grounded/air input distinction and launch ordering.
- `.agents/common-mistakes/dashjump-launch-parity.md` — coyote-height and platform carry parity for impulse launches.
- `.agents/common-mistakes/physics-capture-must-prove-runtime-and-input.md` — runtime, artifact, input, and route evidence requirements.
- No `.agents/standards/` directory or standards index exists in the current checkout; `AGENTS.md` is the project-level guide.

## Open questions (CONSIDER from review)

- During Inc 1, confirm the existing `World` query result exposes stable face/normal provenance for telemetry; if not, extend that return shape instead of issuing a second diagnostic query.

## Out of scope

- Rewriting the camera, renderer, map streaming, or model animation system.
- Porting all of Celeste64 or converting the C# runtime wholesale.
- Replacing the collision mesh or changing the N64 controller layout without separate evidence and a separate decision.
- Another blanket delete-first rewrite, broad “cleanup” pass, or tuning constants before Inc 1 has isolated the actual failure.
