# Restore controls to the "good" feel

## Context

The user reports controls are "still really bad" after a prior effort restored
the good-era movement. They asked to investigate two commits:

- `4551ab4` ("feat: forsaken city map + refact: controls") — the overhaul that
  introduced `InputSystem` and added skid/slope/ledge/ice feel branches.
- `ab6a6607` ("Update README: document baked level pipeline…") — **README-only**,
  irrelevant to controls.

**Investigation result (verified):** the working tree is already byte-equivalent
to the good-era commit `8ea095c` for the movement path:

- `player_controller.cpp` ≡ good era (only refactor: `MoveToward`→`Approach`,
  helpers moved to `runtime/math.hpp`, unused `room` param).
- `movement_config.hpp` **identical** to good era.
- Input system restored: `kStickMax=80`, no move deadzone, no D-pad, camera
  signs fixed. `SampleStickAxis` + `InputActionVec2::Update(deadzone=0)` ≡
  good-era inline `ReadPlayerInput` (both clamp a full diagonal to 1.0).
- `InputButton::Pressed()` = `pressed_ && !press_consumed_`; the controller
  reads raw `input.jump_pressed` (no `ConsumePress`), so it ≡ good-era
  `pressed.a`.
- ROM (20:23) is newer than the source edits (20:02–20:03) → the ROM contains
  the restored code. Removed symbols (`Skid`, `LedgeAssist`, `SlopeSpeed`,
  `ToProfile`) are absent from the ELF.
- Frame rate is 59.9 fps — **not** a perf problem. Both ROMs load the same
  `forsyken-city` map.

**Conclusion:** the movement/input code is already restored. If the user still
perceives bad controls, the cause is one of:

1. The "good" feel they remember is from a **different era** (e.g. the May
   `acf5f03` commit, which used 10×-smaller pre-scale values), not `8ea095c`.
2. A **collision/motor** difference (one-way/ice detection added in the
   overhaul) changes grounded feel on the actual map.
3. A **perception/emulator** issue (Ares input config, controller deadzone on
   the physical pad).

This plan is a **diagnostic-first** plan: it isolates which of these is true
before changing any code, then applies the minimal targeted restore.

## Architectural decisions

- Decision: **Do not re-revert the movement code** — it is already byte-equal
  to good era. Re-reverting would be a no-op. Source: code diff verified.
- Decision: **A/B test first** using the existing reference ROM
  `good_era_8ea095c.z64` (hash `85733687…`) vs the current
  `madeline_cube_rom.z64` (hash `42f4ccb2…`). This is the single highest-value
  step: it tells us whether the good-era reference ALSO feels bad (→ different
  era) or only the current ROM (→ collision/render layer). Source: user-confirmed
  the current ROM feels "identical to bad build" previously.
- Decision: If the good-era reference feels bad too, the "good" memory is from
  a different commit. The strongest candidate is the May `acf5f03` era (10×
  smaller values, pre-10x-scale). We restore those values **only** if the A/B
  test points there. Alternatives rejected: guessing values without a test.
- Decision: If only the current ROM feels bad, the difference is in the
  collision/motor layer (one-way/ice) or the renderer. We bisect by toggling
  the one-way/ice collision detection off (it is the only behavior change in
  the motor vs good era). Alternatives rejected: reverting the whole motor
  (would lose one-way/ice which are spec-canonical and tested).

## Assumptions and answers from code

- Decision: `ab6a6607` is out of scope (README-only). Source: `git show --stat`.
- Decision: Frame rate is not the cause (59.9 fps). Source: `/tmp/ares_telemetry.log`.
- Decision: Both ROMs use the same map. Source: `strings` on both ROMs.
- Decision: The movement code is byte-equal to good era. Source: `diff` of
  working tree vs `git show 8ea095c`.
- Decision: The May-era `acf5f03` values are the fallback "good" candidate.
  Source: `git show acf5f03:src/user/gameplay/movement_config.hpp` (run_speed
  6.4, gravity 60, jump_speed 9.0 — 10× smaller than current 64/600/90).

## Risks accepted

- Risk: The user's "good" memory is from a commit we haven't identified. Mitigation:
  the A/B test with the good-era reference ROM narrows this to "different era"
  vs "current layer"; if different era, we enumerate candidate commits and ask.
- Risk: A/B testing on Ares is subjective (feel). Mitigation: use a concrete
  checklist (full-stick speed, small-deflection response, jump height, dash
  distance, skid presence) and compare side-by-side.
- Risk: Toggling one-way/ice off could break spec tests. Mitigation: gate it
  behind a compile-time flag, run the feel_spec tests, and only ship the toggle
  if it is the confirmed cause.

## Increment DAG

Inc 1 is a branch point: its A/B verdict selects exactly ONE of Inc 2 or Inc 3
(they are mutually exclusive — never both). Inc 4 depends on whichever branch
Inc 1 selected, not both.

- Inc 1 — A/B test reference vs current (S) — depends on: none — unblocks: 2, 3
- Inc 2 — Bisect collision/motor layer (M) — depends on: 1 (only if reference good, current bad) — unblocks: 4
- Inc 3 — Restore May-era feel if A/B points there (M) — depends on: 1 (only if reference also feels bad) — unblocks: 4
- Inc 4 — Verify + ship (S) — depends on: 2 XOR 3 (the branch Inc 1 selected) — unblocks: none
- Inc 5 — Enumerate candidate commits + ask user (S) — depends on: 1 (only if neither 2 nor 3 confirms) — unblocks: 4

## Increments

### Inc 1 — A/B test reference vs current (S)
**Depends on:** none
**Unblocks:** 2, 3
**Done criteria:** user reports whether `good_era_8ea095c.z64` feels different
from `madeline_cube_rom.z64` on a concrete checklist.

#### Files to touch
None (test-only). Uses the two existing ROMs in the repo root.

#### Edge cases
- If the user cannot tell them apart, both feel the same → the "good" era is a
  different commit (go to Inc 3).
- If the reference feels good and current feels bad → the difference is in the
  collision/render layer (go to Inc 2).

#### Verification
- Run: launch `good_era_8ea095c.z64` and `madeline_cube_rom.z64` in Ares
  (per AGENTS.md launch command), side by side.
- Checklist: (1) full-stick run speed, (2) small-deflection response,
  (3) jump height, (4) dash distance, (5) skid on reverse, (6) wall grab.
- Done: user records a clear verdict for each ROM.

### Inc 2 — Revert collision layer to good-era behavior (M)
**Depends on:** 1 (only if reference good, current bad)
**Unblocks:** 4
**Done criteria:** the collision query behavior matches good-era `8ea095c`
(no `RaycastMeshVertical` fast path, 9-point floor probe, 3-slice wall query,
upward ceiling ray, good-era `best_world_t`), while keeping the telemetry
plumbing (`query_counters`, `out_nodes_touched`) so the build and profiler
still work.

**STATUS: DONE** — implemented 2026-08-12. `world.cpp` restored to good-era
behavior (removed `RaycastMeshVertical` fast path, restored `QueryFloorSource`
→ `RaycastRoomSource`, restored upward `QueryCeilingSource`, restored 9-point
probe, restored 3-slice wall query). `coll_mesh.cpp` restored `best_world_t =
h.t * max_t` (threaded `max_t` through `RaycastLeafTriangles`). Telemetry
(`query_counters`, `out_nodes_touched`) kept. 25/25 host tests pass; ROM builds
clean (exit 0). Manual A/B on device still pending.

**Why this changed from the original one-way/ice bisection:** the baked
`forsyken-city` map has **ZERO one-way and ZERO ice faces** (0 oneway, 0 ice,
206 death, 10618 solid — verified by parsing the colmesh). The one-way/ice
toggle would change nothing on this map, so the bisection was vacuous. The
user chose to revert the collision layer to good-era wholesale instead.

#### Files to touch

##### src/user/gameplay/world/world.cpp
- What changes: restore good-era collision *behavior* while keeping the
  `query_counters` telemetry:
  - `RaycastRoomMesh`: remove the `RaycastMeshVertical` fast path — always use
    `RaycastMesh` (good-era). Keep the `query_counters` increment.
  - `QueryFloorSource`: restore to call `RaycastRoomSource` (good-era) instead
    of the inlined mesh-only + collider loop.
  - `QueryCeilingSource`: restore the upward raycast `{0,1,0}` via
    `RaycastRoomSource` (good-era) instead of calling `QueryFloorSource`
    (downward) + normal check.
  - `ProbeFloorDebug`: restore the 9-point grid (good-era) instead of the
    5-point probe.
  - `QueryWalls`: restore the 3-slice wall query (good-era) instead of the
    single horizontal slice.
- Function(s): `RaycastRoomMesh`, `QueryFloorSource`, `QueryCeilingSource`,
  `ProbeFloorDebug`, `QueryWalls`.
- Integration points: called from `PlayerMotor::Step` and `WorldCollision`;
  no signature change.
- Error paths: none — restores good-era behavior.

##### src/user/gameplay/physics/coll_mesh.cpp
- What changes: restore good-era `best_world_t = h.t * max_t` in
  `RaycastLeafTriangles` (the current recompute via `max_t_local` from a
  non-zero axis is a behavior change). Keep `RaycastMeshVertical` defined
  (still referenced by the header) but it is no longer called from
  `RaycastRoomMesh`.
- Function(s): `RaycastLeafTriangles`.
- Integration points: called from `RaycastMeshGeneric`/`RaycastMeshVertical`.
- Error paths: none.

#### Edge cases
- The `query_counters` telemetry must still compile and run (the scene and
  motor reference it). Keep the `nodes_ptr`/`out_nodes_touched` plumbing.
- `RaycastMeshVertical` stays in the header (API compat) but is unused by the
  hot path.
- Host tests that assert on the 5-point probe / single-slice wall behavior may
  need updating to the good-era 9-point / 3-slice behavior.

#### Verification
- Run: `./tests/run_host_tests.sh` — fix any tests that assert the new probe/
  slice counts.
- Build: `./compile-rom.sh` → A/B against reference.
- Done: user confirms the current ROM's grounded feel matches the reference.

### Inc 3 — Restore May-era feel if A/B points there (M)
**Depends on:** 1 (only if reference also feels bad)
**Unblocks:** 4
**Done criteria:** the movement *feel* matches the May `acf5f03` era, re-scaled
×10 to the current world, and the user confirms the feel is restored.

#### Files to touch

##### src/user/gameplay/player/movement_config.hpp
- What changes: restore the May-era `MovementConfig` *feel* by re-scaling the
  May-era values ×10 to match the current 10×-scaled world. The May-era raw
  values (run_speed 6.4, acceleration 50, gravity 60, max_fall_speed -12,
  jump_speed 9.0, dash_speed 14) are pre-scale; the current world is 10×-scaled
  (per `0870e1d`), so the applied values are the ×10 equivalents (run_speed 64,
  acceleration 500, gravity 600, max_fall_speed -120, jump_speed 90, dash_speed
  140). **These ×10 values are what the current `MovementConfig` already holds**
  — so this increment is a *confirmation probe*: it verifies the May-era feel
  is the target by A/B-ing the current (already ×10) values against the
  reference, and only if the user confirms the May-era feel is "good" do we keep
  the current values and close. If the user instead wants the raw pre-scale
  feel, that requires a world-scale change and is out of scope (see Out of
  scope).
- Function(s): struct member defaults only (no change expected if the ×10
  values already match).
- Integration points: consumed by `PlayerController`/`PlayerMotor`.
- Error paths: if the world is 10×-scaled and the raw pre-scale values were
  applied, the player would barely move — this increment does NOT apply raw
  values; it confirms the ×10 feel.

#### Edge cases
- The current world is 10×-scaled (per `0870e1d` "Scale world to 10x OG
  units"). The May-era *feel* is the target, expressed as ×10 values (which the
  current config already holds).
- If the A/B test does not point to the May era, skip this increment (go to
  Inc 5).
- If the user confirms the May-era feel is "good" and it equals the current
  values, Inc 3 is a no-op confirmation and Inc 4 ships the current state.

#### Verification
- Run: `./tests/run_host_tests.sh` — movement tests should pass (values
  unchanged).
- Build: `./compile-rom.sh` → A/B against reference.
- Done: user confirms whether the May-era feel (current ×10 values) is the
  "good" one.

### Inc 4 — Verify + ship (S)
**Depends on:** 2 XOR 3 (the branch Inc 1 selected)
**Unblocks:** none
**Done criteria:** the confirmed fix is applied, all host tests pass, ROM builds
clean, and the user confirms the controls feel good.

#### Files to touch
Depends on which branch was confirmed:
- **If Inc 2 confirmed** (one-way/ice is the cause): ship with the flag
  **default OFF** (or remove the one-way/ice code entirely) so the bad behavior
  stays disabled. Do NOT remove the flag and silently re-enable it.
- **If Inc 3 confirmed** (May-era feel is the target): the current ×10 values
  already match; ship the current state unchanged.
- **If Inc 5 confirmed** (a different commit): apply that commit's values.
Re-run the full suite after finalizing.

#### Verification
- Run: `./tests/run_host_tests.sh` (all pass), `./compile-rom.sh` (exit 0).
- Manual: Ares walk-through of the full checklist.
- Done: user confirms controls feel good.

### Inc 5 — Enumerate candidate commits + ask user (S)
**Depends on:** 1 (only if neither Inc 2 nor Inc 3 confirms)
**Unblocks:** 4
**Done criteria:** a short list of candidate "good" commits is presented to the
user, and the user identifies which one had the good feel.

#### Files to touch
None (research + user question). Enumerate commits between `8ea095c` and the
May era (`acf5f03`) that touched movement/input, and present them with their
movement values for the user to pick.

#### Edge cases
- If the user cannot identify any candidate, the "good" feel may be from a
  commit before the May era or a non-committed state; ask the user for a
  description of the feel (speed, jump height, dash) to narrow it.

#### Verification
- Done: user identifies the commit (or describes the feel) that had the good
  controls.

## Cross-cutting verification
- After Inc 1, the A/B verdict drives which of Inc 2/Inc 3 runs. Do not run both
  blindly — the A/B result selects exactly one path (2 XOR 3).
- If neither Inc 2 nor Inc 3 confirms, run Inc 5 (enumerate candidate commits +
  ask the user) before Inc 4.
- After the confirmed fix (Inc 4), re-run the full host suite + ROM build and
  do a final Ares walk-through.

## Standards / common-mistakes referenced
- `docs/controller_spec.md` §10/§12/§13 (analog normalize, ground movement,
  analog magnitude) — the feel contract.
- `docs/movement_spec.md` — current movement constants.
- No `.agents/standards/` index exists in this repo; `.agents/common-mistakes/`
  topics (camera-respawn-reset, dfs path, player-start-init, winding) do not
  apply.

## Open questions (CONSIDER from review)
- The May-era values are pre-10x-scale; the current world is 10×-scaled. Inc 3
  now treats the May-era *feel* (×10 values, which the current config already
  holds) as the target, so this is resolved as a confirmation probe rather than
  a raw-value apply.
- The user's "good" memory may be from a commit between `8ea095c` and the May
  era that we have not enumerated. Inc 5 covers this: if neither the reference
  nor the May-era probe matches, enumerate all candidate commits and ask the
  user.
- Inc 2's bisection is only meaningful if the baked `forsyken-city` map
  actually contains `MAT_ONEWAY`/`MAT_ICE` faces. Verify this before relying on
  the toggle; if absent, go to Inc 5.

## Out of scope
- Re-reverting the already-restored movement code (it is byte-equal to good era).
- Renderer/perf work (frame rate is already 59.9 fps).
- The `ab6a6607` README commit.
- Applying raw pre-scale May-era values to the 10×-scaled world (would break
  movement; the May-era *feel* is expressed as ×10 values instead).
