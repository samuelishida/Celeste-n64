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
- Standalone renderer/perf tuning; latest captures are ~30 FPS, so if controls
  still feel delayed after the multi-tick input fix, measure that separately
  before attributing it to movement constants.
- The `ab6a6607` README commit.
- Applying raw pre-scale May-era values to the 10×-scaled world (would break
  movement; the May-era *feel* is expressed as ×10 values instead).

## Follow-up: fixed-tick press replay (2026-09-25)

The earlier 59.9 FPS reading is stale for the current world: the latest Ares
captures report about 30 FPS, and the shared frame input snapshot is replayed
for every 60 Hz physics tick. A copied `PlayerInput` reset its local consume
flag, while `ConsumeJumpPress` ignored failure from the shared device edge.
SkidJump also retained coyote time, unlike OG `SkidJump()`; a repeated edge on
the following fixed tick could replace the 120-speed skid jump with a normal
90-speed jump.

Reproductions failed before the fix:

- `input_system_smoke`: two sibling tick snapshots both claimed one device
  jump edge;
- `skid_smoke`: skid jump left coyote time active.

Fix now consults shared device-edge availability, requires successful
consumption before action transitions, and clears coyote time/restores its
recorded height on skid jump. The opt-in input trace now prints each tick with
render-frame/substep grouping and edge availability/consumption. Validation:
67/67 host tests and normal ROM build pass. Still open: reload current ROM in
Ares, verify actual controller feel, and compare `good_era_8ea095c.z64` against
current on the concrete checklist above; this follow-up does not close Inc 1 or
the manual acceptance gate.

## Follow-up: OG move deadzone restored for slop (2026-09-25)

The active physics-fix request reopened the earlier no-deadzone choice. Source
inspection found OG `ControlsConfig::Defaults.Move.Deadzone=0.35`, while the
demake accepted every nonzero stick value. Because `RelativeMoveInput`
normalizes direction and `AnalogMagnitude` maps any active input below 0.4 to
at least 30% speed, tiny raw deflection/noise could target 19.2 units/s.
`InputSystem::move_deadzone` now defaults to OG's 0.35 circular threshold;
explicit A/B can still set it to 0. Added a host regression that failed before
the change at the default-value assertion and now verifies tiny input is
zeroed while raw values remain observable. Full suite: 79/79; normal and
isolated autoplay ROMs build. Fresh Ares autoplay capture
`build/baseline-og-deadzone-20260925-100813.txt` qualifies against SHA-256
`c4fa94ea8d936730da83fd6223fa3c65d9f57d7846555eccb261a29c54244a54`, with
map/material/render health and movement, air, jump/dash, guided climb,
wall-jump, and clearance gates passing. Autoplay bypasses live stick polling;
physical low-stick feel remains unmeasured. Live trace was throttled to 6-tick
samples plus bucketed stick/trigger/button/state/contact transitions to keep
Ares serial output bounded.

## Follow-up: current-ROM requalification + support telemetry (2026-09-25)

The live trace now emits quantized ground/climb normals, support-surface and
grip-face IDs, previous-grounded state, and ground-snap cooldown. Contact/normal
changes trigger immediate samples; unchanged movement stays at 10 Hz. Host
regression verifies normal bucketing. Current trace ROM SHA-256
`325b7251d434ed581ddaa8a12c3b737cf5bab47ebf62e03c4ef619f34a0465d3` was
captured in Ares at
`build/baseline-physics-trace-support-fields-20260925-123950.txt`: 45/45 samples
include the new fields, raw input remains neutral, and player stays grounded
and stationary.

Current normal ROM SHA-256
`f0110431ff4d69f396a55cb91c2e481316f98295108e0f63fd8b4d68bb56114b` boots
cleanly at `build/baseline-physics-resume-normal-boot-20260925-124138.txt`.
Fresh isolated Ares route gates pass against exact artifact identities:
wall-jump `328fb3c89043592f5bf110dd7eb277b1c6afd4cf9bd13795474fa318e7579279`
and mantle `b10a91668a141b1a3975a8b3207d855905ba25e0b1656b90e2cc1c6176247ff7`.
Full host suite passes 79/79. User's supplied manual log has `z=1` through idle
samples and climb start, so it is not neutral-input evidence; unattended Ares
trace reads all-zero shoulders. These results close runtime/build regressions,
not subjective live-controller feel or the pending good-era A/B verdict.

Read-only Ares `VirtualPad1` inspection found `R-Trigger` mapped to gamepad
axis 2 low (`0x12dc83106/0/2/Lo`); supplied idle log pairs `z=1` with
`ar=200`. `InputSystem` consumes digital `z/l/r` and ignores analog triggers,
but this correlation alone does not prove that the axis binding caused raw Z.
Inspect the live Ares mapping that feeds N64 Z and capture deliberate
press/release transitions before changing input code or controller mappings.

## Follow-up: fixed-step hitch preservation (2026-09-25)

The supplied Ares trace reports a `121.662 ms` streaming peak. Previous
`FixedStepAccumulator` discarded time above its five-tick (`83.3 ms`) per-frame
work cap, losing about `38 ms` per hitch of that size. It now preserves up to
ten ticks of bounded backlog, keeps the five-tick execution cap, and includes
pending/dropped time in input-trace scheduling telemetry. Regression tests
cover a 120 ms hitch/catch-up, a 1 s stall/drop bound, and invalid deltas.
Normal ROM SHA-256 `2a745f8e821feb124407859922d432362afb620ef9bfd29656827748628aa86c`;
trace ROM SHA-256 `78beb872bd6a9574a6ffe00185da2f65fba9feb214e0eed814d7ba2034d7ca65`.
The currently open Ares still runs the prior trace ROM; manual live-input
acceptance remains pending on the rebuilt trace artifact.

## Follow-up: disambiguate controller style and raw shoulder bits

The supplied trace paired `z=1`, `r=0`, and `ar=200`; Ares settings also show
an axis mapped to `R-Trigger`, but logs did not identify the active libdragon
joypad style or distinguish current from stable-held digital masks. The input
trace now records `joypad_get_style()`, `joypad_get_buttons()`, and
`joypad_get_buttons_held()` at periodic samples and exact climb start. Use the
new fields to attribute Z/R/L before changing game controls or asking the user
to remap anything. Ares exposes distinct Z and R inputs; libdragon's N64 style
derives analog R from digital R, while GameCube style reports an independent
analog trigger. No input behavior changed.

## Follow-up: fresh trace + route requalification (2026-09-25)

Host suite passes 79/79. Capture regression now proves `[input-trace]` records
survive `tools/capture_baseline.sh`. Normal ROM
`5588babc1194eccd979105fc693d91b1a0288ed8b074eb79a3c158d1c6149b66` boots all
45 map cells, 10,618 collision triangles, 32 materials and texture rooms;
stationary counters show 56 near batches / 13 resident cells. Current
interactive trace ROM is `physics_input_trace.z64`, SHA-256
`c125799c4a2f9e1f7ddcacf452ca24418c7b223865925ecc2356211853781605`.
Unattended Ares capture contains 131 neutral N64 samples with current and held
button masks; this proves clean idle only.

Current isolated wall-jump and mantle autoplay captures pass movement,
air-control, real-map wall approach, grounded climb, and zero model-clearance
violations. Autoplay bypasses live pad input, so neither closes Inc 1's
controller-feel gate. Trace capture also caught one 283.3 ms startup hitch; 5
ticks ran, 5 remained pending, and 116.7 ms was reported dropped past the
scheduler's bounded accumulator. Keep this separate from physics feel;
investigate if it recurs during normal play.

Supplied 10:29 ROM log loads map textures but has `z=1` at idle and climb
start, so it cannot prove neutral-input behavior. Older 03:01 log is stale and
fails room/material loading. Next decisive check is live input on current
trace SHA: verify N64 `style1`, `current0000`, `held0000`, and `z=l=r=0` at
rest, then run/release/reverse, tap/hold A, dash, approach wall, and
hold/release Z. Capture `[input-trace]`, `[climb] START`, `[move]`. Do not
change controller mapping or physics based only on `ar=200`; latest raw masks
will locate whether Z is physically asserted. Until live input is observed,
manual feel, neutral Z attribution, and user's good-era A/B verdict remain open.

## Follow-up: real-map corner handoff regression (2026-09-25)

The real baked edge near Start exposed a failure missed by the synthetic
corner fixtures: during the 0.2 s corner ease, reacquiring the old face and
running normal grip/facing gates could snap back, pop out, or exit climbing;
full-reach rays could also nominate an unreachable anchor. The handoff now
keeps its adopted destination face/grip, allows explicit climb release, bounds
anchor reach, and restores the pre-adoption position as the ease origin.
`tests/real_map_corner_traverse_smoke.cpp` now exercises normal entry and
side-walk into that edge. Latest run: one corner event, destination normal
adopted, zero exits, max step 1.866, waist pushout 0.296, model gap 0.500,
13 model samples. Full host suite: 80 passed, 0 failed. This closes the
deterministic real-map handoff regression; live controller feel remains open.

## Follow-up: live no-Z capture locates Ares mapping path (2026-09-25)

Current trace ROM SHA-256
`eaa6925011db361d15b9d6c231deca32577b09b67f0a662df2e7e0e5f46f8bd8` was
captured in Ares at
`build/interactive-physics-input-trace-followup-20260925.log`. Boot reports
`current0000`, `held0000`, `z=0`. On live stick movement, current raw mask
becomes `0x2000`; next sample has `held=0x2000`, `z=1`. User confirms no Z
press. `InputSystem` consumes this `held.z` bit directly, so unintended climb
is input-originated, not proof of a physics latch bug. In the same capture,
climb enters at face 5178 and reaches mantle; visual clearance remains unproven.

Exact Ares route is now verified: N64 `Z` maps from virtual `R-Trigger` in
[Ares N64 input wiring](https://raw.githubusercontent.com/ares-emulator/ares/master/desktop-ui/emulator/nintendo-64.cpp#L34-L37).
Active `settings.bml` binds `VirtualPad1` `R-Trigger` to physical axis 5 high
and button 4. This resolves the emulator-to-game path; telemetry cannot say
which physical binding fired. Next: isolate/rebind those Ares inputs and
require zero Z/L/R while idle and moving the stick/A. Keep game input behavior
unchanged; masking Z would break intended climbing. Shared Ares settings have
not been modified.
