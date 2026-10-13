# Climb animation alignment follow-up

## Context
After the movement port in `.plans/celeste-movement-port/plan.md` (Inc 1–8), the climb physics and wall-angle transitions are correct, but the rendered model does not visibly rotate with the wall when switching angles during a climb. Investigation shows `StatePhase` in `src/user/gameplay/player/player_controller.cpp` bypasses `state.facing = RotateTowardXZ(state.facing, state.target_facing, ...)` whenever the pre-handler state is `Climbing` (line ~1318). During a 90-degree outer corner adopt, physics sets `target_facing` to the new wall normal immediately and even runs a 0.2 s position/facing ease in `StClimbingUpdate`, but the visual `player.facing` only catches up after leaving climb. The renderer derives yaw directly from `player.facing` in `GameplayScene` line ~2373, so the model appears to not move with the wall.

## Architectural decisions
- Decision: Keep gameplay as the authority for both physics and visual facing; do not add a separate renderer-side lerp. Rationale: a single source of truth prevents desync between collision shell and rendered model; OG also updates `facing` inside `Player.Update` before render.
- Decision: Update `state.facing` during climbing using the same `RotateTowardXZ` rate used by normal/dash states, and snap it immediately on hard facing transitions (climb entry, inner/slope adopt, outer-corner ease start). Rationale: we want the visual model to stay glued to the wall direction while preserving the existing smoothing for continuous cases.
- Decision: Preserve the existing `target_facing`/`last_facing` semantics and the corner-ease `target_facing` interpolation. Rationale: these already encode the intended wall direction.
- Decision: Add a host-testable visual-alignment assertion that compares `player.facing` with `player.target_facing` during climb/corner events. Rationale: the bug is invisible to pure physics tests.

## Assumptions and answers from code
- Answered from code: `StatePhase` returns early for climb and therefore never runs `RotateTowardXZ` for climbing ticks (`player_controller.cpp:1315-1318`).
- Answered from code: `StClimbingUpdate` corner-ease branch already interpolates `target_facing` and writes it to `last_facing` (`player_controller.cpp:656-675`).
- Answered from code: `GameplayScene` derives yaw from `impl_->player.facing` (`gameplay_scene.cpp:2371-2375`).
- Answered from code: `ResolvePlayerAnimState` selects `ClimbIdle`/`ClimbUp` from `movement_state == Climbing` and velocity; it does not consume facing (`src/user/gameplay/render/animation_state.hpp`).
- Answered from code: `OnStateEnter` for climbing centralizes entry resets (`player_controller.cpp:1226-1235`).
- Answered from code: `AdoptCornerWall` updates `target_facing` and `last_facing` for inner/slope corner adopts (`player_controller.cpp:139-152`, called from `StClimbingPostMove` ~line 769).
- Answered from code: the outer-corner ease start block resets `target_facing = last_facing = facing_from` before the ease begins (`player_controller.cpp:813-814`).
- Assumption: no climb-specific facing lag is intentional; the movement port gap matrix does not list it as an N64 adaptation.

## Risks accepted
- Fixing facing during climb may reveal z-fighting or model-gap changes on sloped walls because the clearance helpers also read `facing`. Mitigation: run the existing real-map walljump/mantle/corner profiles after the change and assert `min_model_gap` does not regress.
- The 12.57 rad/s rotation rate may feel instantaneous on a 90° switch; if it does, we can add a config field in a follow-up tuning change, not in this plan.
- Rollback: revert the `StatePhase` early-return removal and the three facing-snap sites (`OnStateEnter(Climbing)`, `AdoptCornerWall`, outer-corner ease setup).
- Existing host tests that assert `state.facing` during or after climb may need their expected values updated; a pre-flight grep in Inc 2 will identify them.

## Increment DAG
- Inc 1 — Reproduce and instrument visual facing gap (S) — depends on: none — unblocks: 2
- Inc 2 — Update facing during climb entry, corner ease, and sustain (M) — depends on: 1 — unblocks: 3
- Inc 3 — Add host visual-alignment tests and update acceptance (M) — depends on: 2 — unblocks: completion

## Increments

### Inc 1 — Reproduce and instrument visual facing gap (S)
**Depends on:** none  
**Unblocks:** 2  
**Done criteria:** a host or device capture shows `player.facing` yaw lagging `target_facing` by >10° during a climb entry or 90° corner event.

#### Files to touch
##### `src/user/gameplay/scene/gameplay_scene.cpp`
- What changes: add a `debugf` line `[anim-align] tick=%u facing=%.4f target=%.4f corner_ease=%.4f` when `movement_state == Climbing` and model is loaded, inside the existing render block around line 2370. Emit fixed-tick state (`impl_->player`, not interpolated) using the current `fixed_step.Tick()` counter.
- Function(s): render block before `UpdateMatrix`.
- Data shapes: `uint32_t tick`, `float facing_angle = atan2f(player.facing.z, player.facing.x)`, `float target_facing_angle = atan2f(player.target_facing.z, player.target_facing.x)`, `float corner_ease`.
- Integration points: `impl_->player` read-only; `debugf` routed through existing capture parser infrastructure.
- Error paths: model not loaded → skip line; angles normalize to [-π,π].

##### `tools/verify_physics_capture.py`
- What changes: add a parser for `[anim-align]` lines and emit `climb_max_facing_delta_deg` / `corner_max_facing_delta_deg` when the corresponding tick also contains `corner_triggered=1` or `movement_state=Climbing`.
- Data shapes: regex for `\[anim-align\] tick=(\d+) facing=([\d.-]+) target=([\d.-]+) corner_ease=([\d.-]+)`.
- Error paths: missing lines yield null metrics; verifier tolerates null unless the profile explicitly requires them.

##### `tests/real_map_corner_traverse_smoke.cpp`
- What changes: add a failing host fixture that reproduces the gap without Ares by driving `PlayerController::Step` through a synthetic outer corner. Assert that `AngleBetweenXZ(facing, target_facing) > 10°` at the tick where `corner_triggered` becomes true in the pre-fix code. This gives Inc 1 a red test that turns green after Inc 2.
- Function(s): new test function in existing file or new `tests/climb_animation_alignment_smoke.cpp`.
- Data shapes: synthetic `Room` with two perpendicular walls and a floor, `PlayerController`, fixed input snapshots.
- Integration points: same controller/MoveService test seam used by `climb_corner_smoke.cpp`.
- Error paths: if the controller does not trigger the corner, the test fails with a clear state trace.

#### Edge cases
- Facing may legitimately differ during non-climb states because smoothing is intentional; restrict metric to climbing.
- The host fixture asserts on fixed-tick `PlayerState` after `MovePhase`, not render-alpha values.

#### Verification
- Run: `./tests/run_host_tests.sh` — expect the new `climb_animation_alignment_smoke` test to FAIL in Inc 1, proving the gap.
- Run: `./compile-rom.sh` and Ares corner profile to collect device `[anim-align]` evidence.
- Done: a failing host test and a captured metric prove the gap before the behavior fix.

### Inc 2 — Update facing during climb entry, corner ease, and sustain (M)
**Depends on:** 1  
**Unblocks:** 3  
**Done criteria:** `player.facing` tracks `target_facing` during all climb states; 90° corner switch shows model rotating with the wall; no regression in host tests or real-map routes.

#### Files to touch
##### `src/user/gameplay/player/player_controller.cpp` — `StatePhase`
- What changes: delete the `if (before == PlayerMovementState::Climbing) return;` early return and update the comment above the `!= Dashing` guard so it no longer claims "climb ticks return above, so Climbing never reaches here." The existing guard now covers Climbing too, applying `RotateTowardXZ` every tick while climbing, exactly as for Normal/Skidding.
- Function(s): `StatePhase` tail (~lines 1313-1321).
- Data shapes: `Vec3 state.facing`, `Vec3 state.target_facing`, float `delta_seconds`.
- Integration points: same site as normal/dash smoothing; read `state.movement_state` after possible transitions; `state.facing` consumed by renderer.
- Error paths: zero-length `target_facing` falls back to last_facing via `NormalizeXZ` if needed; `RotateTowardXZ` already handles this.

##### `src/user/gameplay/player/player_controller.cpp` — `OnStateEnter(Climbing)`
- What changes: add `state.facing = state.target_facing` alongside the existing climb-entry resets (`velocity = {}`, `corner_ease = 0`, `input_sign = 1`).
- Function(s): `OnStateEnter` climbing branch (~line 1229).
- Data shapes: `state.target_facing` is already set by `TryClimb` before entry.
- Integration points: centralizes all entry side effects; `state.facing` consumed by renderer.
- Error paths: use the same guard as `NormalizeXZ`: `if (LengthXZ(state.target_facing) > 0.0001f) state.facing = NormalizeXZ(state.target_facing, state.facing);`. If `target_facing` is near-zero or non-finite, `facing` is left unchanged.

##### `src/user/gameplay/player/player_controller.cpp` — `AdoptCornerWall`
- What changes: add `state.facing = NormalizeXZ(state.target_facing, state.facing)` after the existing `last_facing = target_facing` assignment so inner/slope corner adopts do not lag by one frame. Use the same 0.0001f guard.
- Function(s): `AdoptCornerWall` (~line 150).
- Data shapes: `Vec3 target_facing` and `last_facing` already updated.
- Integration points: called from `StClimbingPostMove` inner/slope branch; note that this runs inside `MovePhase` after the climb kinematic sweep, so the render-facing jumps to the new wall while the body position is still mid-sweep. The existing clearance helpers already read `facing`; this is an accepted risk to be measured by the device profile's `min_model_gap`.
- Error paths: if `target_facing` is near-zero or non-finite, leave `facing` unchanged.

##### `src/user/gameplay/player/player_controller.cpp` — outer-corner ease trigger
- What changes: add `state.facing = facing_from` after the existing `state.target_facing = facing_from; state.last_facing = facing_from;` assignment. The ease starts from the old wall direction; `StClimbingUpdate` will then lerp `target_facing` from the old to the new wall over 0.2 s, and the `StatePhase` tail will rotate `facing` toward that moving target. With the 12.57 rad/s cap the smooth follower is nearly instant at 60 Hz, so the net visible rotation is dominated by the ease lerp. The acceptance metric is a measured claim; if it exceeds tolerance, either snap `facing` to the eased target inside the corner-ease branch or add a dedicated climb rotation rate.
- Function(s): `StClimbingPostMove` force_corner/ease block (~line 813).
- Data shapes: `Vec3 facing_from` captured before any wall adopt.
- Integration points: `state.facing` consumed by renderer during the 0.2 s ease.
- Error paths: `facing_from` is always valid because it is captured from `state.target_facing` at tick start.

#### Edge cases
- Dashing from Normal, Skidding, or Climbing snaps `facing` to `target_facing` in `OnStateEnter(Dashing)`, then shared facing rotation continues during dash steering to match OG's player update.
- During corner ease, `StClimbingUpdate` already updates `target_facing` every tick and the `StatePhase` tail rotates `state.facing` toward that moving target. With the 12.57 rad/s cap the smooth follower is nearly instant at 60 Hz, so the net visible rotation is dominated by the ease lerp. The acceptance metric checks the net result, not the individual paths.
- Release from climb into Normal intentionally uses existing Normal smoothing; no climb-specific release snap is added.

#### Verification
- Pre-flight: grep `tests/` for assertions on `state.facing` during/after climb and update or document expected changes before editing controller code.
- Run: `./tests/run_host_tests.sh`, `./compile-rom.sh`.
- Device: repeat corner profile; `climb_max_facing_delta_deg` and `corner_max_facing_delta_deg` should drop below 2°; `min_model_gap` must not regress.
- Done: animation visibly follows wall; no host regression.

### Inc 3 — Add host visual-alignment tests and update acceptance (M)
**Depends on:** 2  
**Unblocks:** completion  
**Done criteria:** a host fixture asserts facing alignment during climb entry, climb sustain, and a 90° corner transition; acceptance docs record the fix.

#### Files to touch
##### `tests/climb_animation_alignment_smoke.cpp` (new)
- What changes: synthetic climb entry onto a wall, then a scripted 90° corner adopt; assert `Angle(state.facing, state.target_facing)` stays below tolerance across the whole window.
- Function(s): fixture uses `PlayerController::Step` with a fake Room and input snapshots, modeled on `tests/climb_corner_smoke.cpp` and `tests/climb_parity_smoke.cpp`.
- Data shapes: two perpendicular finite wall planes at X=0 and Z=0 forming an outer corner; a real horizontal floor; player placed at (-3, y, -3); climb held and move input pushes around the corner.
- Integration points: controller phases, `MoveService`, renderer-agnostic.
- Error paths: fixture provides finite faces and support floor; if the controller does not enter climb, the test fails with a clear state trace.

##### `tests/static_player_runtime_contract.cpp`
- What changes: extend to assert that `ResolvePlayerAnimState` picks `ClimbIdle`/`ClimbUp` even when `target_facing` differs from `facing`, ensuring animation selection is decoupled from the visual fix. If the existing contract file is source-only, add a new header-only test under `tests/` (e.g., `animation_state_priority_smoke.cpp`) and register it in `run_host_tests.sh`.

##### `docs/physics_feel_acceptance.md` / `docs/og_port_gap_matrix.md`
- What changes: add row for visual-facing alignment; mark it matched after device capture; update the corner route acceptance to mention `corner_max_facing_delta_deg`.

#### Edge cases
- Host fixture cannot assert pixels; assert fixed-tick yaw convergence.
- Keep test independent of libdragon/T3D.

#### Verification
- Run: `./tests/run_host_tests.sh`.
- Ensure `tests/climb_animation_alignment_smoke.cpp` is registered in `tests/run_host_tests.sh` (Pattern B with `GAMEPLAY_LINK`) before this increment is merged.
- Done: new test passes and existing suite remains green.

## Cross-cutting verification
- Each increment runs host tests and ROM build.
- Device acceptance repeats walljump, mantle, and corner profiles; new metric is `climb_max_facing_delta_deg`; `min_model_gap` from the corner profile must not regress.
- Manual Ares play confirms no visible lag on 90° wall switches.
- If the project stores bit-exact input replays/autoplay baselines, re-record or bless them after Inc 2.

## Standards / common-mistakes referenced
- `AGENTS.md` — build, controller mapping, capture runbook.
- `.agents/common-mistakes/sweep-contact-flags-must-accumulate.md` — maintain single ownership of state writes.
- `.agents/common-mistakes/visual-facing-must-track-target-facing.md` — rendered yaw must follow target-facing in every movement state.

## Open questions (CONSIDER from review)
- Verify whether OG snaps facing on climb entry or smooths it. If OG smooths every tick including climb, the `OnStateEnter` snap is a deliberate demake choice for visual responsiveness, not source parity.
- Consider adding a `climb_facing_rotate_rate` config field if the default 12.57 rad/s feels too fast on 90° switches.
- Consider whether the renderer should interpolate yaw at render-alpha for even smoother motion; this is out of scope for gameplay correctness.
- Review whether recorded input replays or autoplay routes are bit-exact regression assets; the `StatePhase` facing change will alter replay determinism for recorded input traces during climb. If they exist, re-record or bless them in Inc 2.

## Out of scope
- New climb animation clips or blending.
- Camera-relative facing during climb.
- Re-tuning rotation rate; if needed, a separate tuning PR.

### Animation-selection and dash-facing follow-up — 2026-10-07

The user reported broader Madeline animation problems after the initial
wall-facing fix. Source comparison against `Celeste64-og/Source/Actors/Player.cs`
found that the C++ controller skipped facing updates during Dashing, did not
snap to dash launch direction, and did not select the already-exported `Skid`
clip. The scene also selected climb motion from vertical velocity alone, so
lateral climbing stayed in `Climb.Idle`; clip rate did not match OG; and clip
changes did not reset the skeleton pose.

Implemented: rotate facing in all states; snap at dash entry; map `Skid` stream
`.8.sdata`; select climb movement from total kinematic climb speed; use OG's
1.8 climb rate and speed-mapped run rate; reset skeleton to bind pose on clip
switch. Added host coverage for Skid/lateral-climb selection and dash launch
facing. The original scope's no-new-clips/no-blending decision remains intact.

Verification: host suite **89/89**; normal ROM SHA-256
`a5949b6c8ef31b5ad9d40575ee7125643ab984920cd1fc60cd4dd3742a59cb49`;
Windows Ares boot loaded `player.t3dm` with **7 clips** and successfully booted
the world. Manual visual/controller acceptance is still open.
