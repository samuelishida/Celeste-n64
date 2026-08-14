# Restore controller input feel to pre-overhaul state

**Status: DONE** — all increments implemented. Host tests 25/25 pass; `./compile-rom.sh` builds clean (exit 0, no warnings). Manual on-device (Ares) checklist still pending (steps below).

## Context

The Aug 11 2026 commit `4551ab4` ("feat: forsaken city map + refact: controls")
introduced `InputSystem` (`input_system.cpp/.hpp`, `input_button.hpp`) and
replaced the scene's inline `ReadPlayerInput()`/`ReadCameraInput()`. After that
change the controls feel bad: the player never reaches full run speed, small
stick deflections are swallowed by a new deadzone, and D-pad was added to Move.
The user wants the input *feel* restored to last month (pre-overhaul) while
**keeping** the movement mechanics (skid, slope multiplier, ledge assist,
one-way, ice, dash-jump) that were added in the same commit and are
spec-canonical with passing tests.

Intended outcome: analog stick full deflection → unit magnitude = 100% run
speed; no move deadzone; no D-pad contribution to Move; camera zoom direction
unchanged (C-up zoom-in); all movement mechanics intact.

## Assumptions and decisions

- Decision: Restore move stick scaling to `kStickMax = 80`, remove the
  deadzone pass on the move axis, and magnitude-normalize a full deflection to
  unit magnitude — matching pre-overhaul `ReadPlayerInput()`. Source: user-confirmed.
- Decision: Remove D-pad contribution to the Move action (not present last
  month). Source: user-confirmed.
- Decision: Keep camera zoom direction as-is (C-up → zoom-in, C-down →
  zoom-out). Source: user-confirmed.
- Decision: Do NOT touch movement mechanics (skid, slope, ledge assist,
  one-way, ice, dash-jump) or their tests. Source: user-confirmed + code @
  `tests/feel_spec/`, `docs/controller_spec.md` §16/§42/§43.
- Decision: Keep the new deadzone on the *camera* C-stick/C-button axis — the
  regression is move-stick feel, not camera. Source: default (camera zoom
  direction unchanged implies sampling axis is not in scope).

## Files to touch

### src/user/gameplay/input/input_system.hpp
- What changes: add a host-compilable pure helper so the smoke test can exercise
  the real move-scaling logic (input_system.cpp includes `<libdragon.h>` and
  cannot compile on the host). Also fix the stale doc comment.
- Function(s):
  - `Vec2 SampleStickAxis(int16_t stick_x, int16_t stick_y, float stick_max)`
    — pure, inline. Returns **raw per-axis normalization only** (NOT a final
    movement vector): `{ -clamp(x,max)/max, clamp(y,max)/max }` with the
    project convention `stick right -> +X`, `stick up -> +Y`. It does NOT apply
    the deadzone or the unit-magnitude clamp — both are applied downstream by
    `InputActionVec2::Update` (which clamps a full diagonal to magnitude 1.0).
    Host-testable. The name is deliberately "Axis" (not "Move") so callers know
    it returns raw normalized coords, not the resolved movement input.
  - Update class doc comment "Move -> left analog stick + D-Pad (digital
    cardinals)" to "Move -> left analog stick" (D-pad removed).
  - Add a brief comment above `SampleStickAxis` noting D-pad is intentionally
    NOT sampled here (removed to match pre-overhaul feel), so a future reader
    does not re-add it.
- Integration points: called by `SampleMoveAxis` in the .cpp.

### src/user/gameplay/input/input_system.cpp
- What changes: restore analog move sampling to pre-overhaul behavior; drop
  D-pad → Move; leave camera sampling and button aliasing unchanged.
- Function(s):
  - `SampleCameraAxis(InputActionVec2& camera, const joypad_inputs_t& inputs)` —
    unchanged in behavior. Leave its inline `90.0f` constant as-is; add a brief
    comment there noting it intentionally differs from the move axis' `80.0f`
    (camera feel is out of scope) so the asymmetry is visible from both sides.
  - `constexpr float kStickMax = 90.0f;` → `constexpr float kStickMax = 80.0f;`
    (80 matches pre-overhaul feel; add a comment noting the camera axis
    deliberately stays at 90 so a future cleanup does not re-unify them).
  - `SampleMoveAxis(InputActionVec2& move, const joypad_inputs_t& inputs)` —
    modified (drops the `float deadzone` parameter, now unused):
    - call `SampleStickAxis(inputs.stick_x, inputs.stick_y, kStickMax)`
    - call `move.Update(s.x, s.y, 0.0f)` (no deadzone; this is where the
      unit-magnitude diagonal clamp happens)
    - remove the four `if (inputs.btn.d_*) move.AddDigital(...)` lines
  - `InputSystem::Poll()` — update the `SampleMoveAxis(move, inputs, deadzone)`
    call to drop the deadzone argument: `SampleMoveAxis(move, inputs)`.
- Data shapes: unchanged (InputActionVec2 `move`, joypad_inputs_t).
- Integration points: called from `InputSystem::Poll()`; consumed via
  `MoveValue()` in `gameplay_scene.cpp::ReadPlayerInput`.
- Error paths: none new — the move axis never deadzones, so zero-stick drift
  maps to magnitude `0.0` (inside `InputActionVec2::Update`, the length `< 0`
  branch still zeroes it). A slightly-off-center resting stick now reports a
  small nonzero move; acceptable and matches last month.

### AGENTS.md
- What changes: one-line control-map doc fix so it no longer claims D-pad moves
  the player. Under "Current ROM control map" change "left analog stick +
  D-Pad: move" to "left analog stick: move".

## Edge cases
- Full diagonal deflection: `InputActionVec2::Update` already clamps combined
  magnitude to 1.0 (§12) — no change, still safe.
- Stick drift at rest with no deadzone: `x`/`y` from `-stick/80`; a resting
  stick of ~±5 → ±0.06 magnitude → ~30% move per `AnalogMagnitude`. Matches
  pre-overhaul behavior exactly.
- C-stick still uses the deadzone: camera input unaffected by this change.
- Buttons (`A/B/Z/Start`, GameCube aliases) untouched.

## Verification
- Run: `./compile-rom.sh` (full ROM build) — must exit 0.
- Tests to add/update: extend `tests/input_system_smoke.cpp` with a host test
  `TestMoveSampling` that exercises the **actual changed logic** via the new
  pure `SampleStickAxis` helper (host-compilable) AND the real downstream clamp:
  - `SampleStickAxis(85, 0, 80)` → `{-1, 0}` (cardinal right, magnitude 1.0).
  - `SampleStickAxis(0, 85, 80)` → `{0, +1}` (stick up -> +Y).
  - `SampleStickAxis(0, 0, 80)` → `{0, 0}` (no deadzone, zero at rest).
  - `SampleStickAxis(5, 0, 80)` → `{-0.0625, 0}` (nonzero; no deadzone, small
    deflection moves).
  - `SampleStickAxis(85, 85, 80)` → `{-1, +1}` (raw per-axis; magnitude ≈
    1.414 BEFORE the unit clamp — this is expected, do not assert 1.0 here).
  - Feed the helper output through `InputActionVec2::Update(s.x, s.y, 0.0f)`
    and assert the FINAL resolved magnitude clamps to ≈1.0 on a full diagonal —
    this guards the real `SampleMoveAxis` flow (`-85`/`85` → `{-1,+1}` →
    `Update` clamps to 1.0). It also fails if someone reverts `kStickMax` to 90
    or re-adds a deadzone to `SampleStickAxis`.
  Because `SampleStickAxis` returns raw normalized coords and D-pad removal is
  a structural change (the `AddDigital` calls are deleted from `SampleMoveAxis`,
  which takes libdragon types and cannot compile on host), the D-pad guard is
  covered by the explicit "D-pad not sampled" comment in the helper + manual
  testing + code review, not the host smoke. Ensure
  `tests/run_host_tests.sh` builds/runs `input_system_smoke` (currently absent
  from the runner); add it. Note: `joypad_inputs_t.stick_x/y` are `int8_t`
  (±85 is in range), and the host test feeds the helper values in that range.
- Manual: launch `madeline_cube_rom.z64` in Ares. (1) Push stick fully right →
  player should hit ~100% run speed (noticeably faster than the ~93% cap).
  (2) Push stick slightly (< deadzone range) → player should still begin to
  move. (3) Tap D-pad → no movement. (4) C-up zooms in, C-down zooms out.
  (5) Run then reverse → skid still appears (mechanics intact).
- Done criteria: full stick deflection produces unit-magnitude move input
  with no deadzone, D-pad no longer moves the player, camera zoom direction
  unchanged, and all feel_spec movement-mechanic tests still pass.

## Standards / common-mistakes referenced
- `docs/controller_spec.md` §10 (analog normalize/deadzone), §12 (ground
  movement), §13 (analog magnitude) — confirms 100% speed at full deflection
  and that deadzone is optional.
- `docs/controller_spec.md` §16 skid / §42 slope / §43 ledge — the mechanics
  we are deliberately NOT reverting.
- No `.agents/standards/` index exists in this repo (only
  `.agents/common-mistakes/` for unrelated topics: camera-respawn-reset, dfs
  path, player-start-init, winding). None apply.

## Estimated scope
S

## Open questions (CONSIDER from review)
- Divergent normalization constants: move uses `kStickMax = 80`, camera still
  hardcodes `90.0f`. Intentional (camera untouched). Now resolved with a comment
  on BOTH the move `kStickMax` and the camera `90.0f` (see Files to touch) so
  the asymmetry is visible from both sides.
- Equivalence vs. pre-overhaul clamping: pre-overhaul code did not clamp the
  stick (raw `-85/80 = -1.06`, normalized to 1.0); the new path clamps to ±80
  then normalizes. Both yield magnitude 1.0 at full deflection, so feel
  matches — confirmed equivalent, no further action.
- Helper contract: `SampleStickAxis` returns RAW per-axis coords; the unit
  magnitude clamp for a full diagonal happens inside `InputActionVec2::Update`.
  The host test asserts raw per-axis output from the helper and the clamped
  magnitude from `Update`, so the two responsibilities are tested separately.
