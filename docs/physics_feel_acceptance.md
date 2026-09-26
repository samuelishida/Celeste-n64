# Physics feel ROM acceptance

Run this on a controller in the Forsaken City map after the host suite is green.

For unattended deterministic coverage:

```sh
bash tools/capture_physics_autoplay.sh [wall_seconds] [label] [walljump|mantle]
```

Append `walljump` (default) or `mantle` as third argument to select isolated
climb-exit profile. Each route has its own terminal gate; wall-jump success
cannot stand in for mantle. It builds isolated ROM, captures Ares telemetry,
then rejects result unless ROM SHA, build marker, map/collision, material
catalog, textured room, render counters, zero raw climb shoulders, and selected
route metrics agree. It refuses to start if an Ares session is already open.
This gate validates mechanics and runtime health; it does not replace manual
controller feel or visual review.

1. Hold run, release stick, then reverse: observe braking and turn/skid; record any delay or unwanted drift.
2. Compare tap jump vs held jump: tap jump must peak visibly lower.
3. Walk off an edge and jump late; press jump just before landing; both forgiveness windows should be clear without double-firing.
4. Dash at forward, 90°, and diagonal camera yaws: aim follows camera-relative stick; check dash hitstop and steering.
5. Ground dash then jump near its end: dash-jump should carry farther than a run jump.
6. Spend air dash: it refills on landing or crystal, not midair.
7. Hold Z/L/R into a wall, climb up, release, wall-jump, mantle, then traverse one corner. Climb grip is unlimited; OG has no stamina drain.
8. Compare partial and full stick deflection on flat ground; movement should retain analog magnitude rather than act binary.
9. Compare partial/full stick while airborne, then release the stick: air direction should target full run speed, while no-input air drift decelerates; grounded movement must still retain analog magnitude.
10. Run up and down the same safe slope, then compare against flat ground: OG slope modulation should slow uphill travel and speed downhill travel without treating near-vertical walls as slopes.

Record: emulator/hardware, controller, date, and any value that feels wrong before freezing constants.

## OG air-control parity check (2026-09-25)

Source audit found air-control mismatches: partial stick used grounded analog
speed, no-input air movement omitted `AirFrictionMult=0.1`, and directional
steering used velocity alignment/rotation instead of OG's target-facing map.
Air now targets full `run_speed`, maps acceleration from input-vs-facing, and
uses `PastMaxDeccel` only for at/above-max velocity-aligned input; it no longer
rotates or overwrites `target_facing`. No-input air friction and grounded analog
scaling remain separate. Directional wall-jump resolution skips same-tick air
acceleration or friction, matching OG ordering.

Regression evidence: both prior failures reproduced before the fix in
`/tmp/hawk-fix-bug-air-friction-before.log` and
`/tmp/hawk-fix-bug-air-analog-repro.log`. `locomotion_smoke` now checks
air-stop velocity 64→24 after 30 fixed ticks, full-speed partial-stick air
steering, target-facing acceleration multipliers, the past-max gate/map,
direct air steering without facing rotation, and unchanged grounded analog
speed. The air-steering regression failed before the source fix and passes after
it. Current host suite passes 78/78. Normal ROM rebuilt with marker
`Sep 25 2026 09:24:50` (SHA-256
`b89740b2e946ba22d226646a1f971bba4090a2754344bdea6309dbd0aec99153`);
isolated autoplay ROM marker is `Sep 25 2026 09:24:51` (SHA-256
`a20d983df8052b5dee20b88e2748f397c281932f9cb7ff4dd6e5e7777b9c6654`).
The user-provided Ares log is not current physics evidence: its build marker is
`03:01:07`, raw climb input is held (`z=1` at rest), and the map renderer
reports zero batches. Human controller feel and a qualified current-ROM device
replay remain pending.

## Integrated autoplay air-control gate (2026-09-25)

The isolated Forsaken City autoplay now steers camera-right for six airborne
ticks, releases for four ticks, then starts its existing dash. `[phys-test]`
records lateral velocity gain, release speed drop, and any grounded tick in
that window; pass requires gain ≥8, drop ≥4, and no ground contact. The
`locomotion_smoke` route fixture replays the same 6+4 tick sequence from a
captured jump velocity and asserts those bounds. Host suite: 74/74, including
the newly registered `slope_smoke`. Isolated
`physics_autoplay.z64` rebuilt (SHA-256
`dbf72d53a396d9e2cb8d249946b965ba4757e9bc3109b64042e8cd296f2e18a6`). Device
replay is pending; existing Ares session was left untouched.

## Air-dash launch parity + current-ROM Ares proof (2026-09-25)

OG applies a normalized 3D air-dash vector `(facing.x, 0.4, facing.z) *
DashSpeed / sqrt(1.16)` on the accepting tick. The demake accepted the state,
then its same-tick state cascade immediately entered `StDashingUpdate`, erased
velocity during local hitstop, and later flattened the launch to XZ. Fixed by
preserving the acceptance-tick vector, starting active duration after hitstop,
and retaining/scaling Y through active dash and expiry. Ground dash stays
horizontal. Tests assert the launch vector and reject a grounded or flat launch
from captured device telemetry.

Validation: `./tests/run_host_tests.sh` — 81/81; `./compile-rom.sh` succeeded.
Normal ROM `madeline_cube_rom.z64` SHA-256
`7bc461ba167b5fb57e372eeb43cfae73dbce40d923a82d2a97329ffda0873dd6`, build
marker `Sep 25 2026 17:37:34`. Ares boot capture
`build/baseline-physics-normal-airdash-20260925-174019.txt` shows all 32 map
materials, textured room batches, nonzero render counters, and neutral `z=0`.
Wall-jump capture
`build/baseline-physics-air-dash-20260925-173742.txt` (ROM SHA-256
`a726af9542e06b5dc5c469fe2e0d019abddf114da72cc093f25ef53ba2f69dc7`) and
mantle capture `build/baseline-physics-air-dash-mantle-20260925-173909.txt`
(ROM SHA-256 `7c41f25cdbd2bd14a556bc0ed5237848c5200501d87e1703879532f564d17825`)
both qualify. Each reports airborne dash speed 140, vertical launch 52, raw
climb shoulders zero, and zero model-clearance violations; walljump and mantle
each pass independently.

These deterministic route/device checks do not establish human controller
feel or visual confirmation. User has not yet replayed the rebuilt normal ROM.

## No-teleport real-map corner route (2026-09-25)

The previous Ares autoplay stopped at wall-jump/mantle and had no device gate
for corner easing. Extended the isolated route with a `corner` profile and
changed `real_map_corner_traverse_smoke` to begin at the manifest `Start`
spawn, walk to the real wall, enter at OG `ClimbCheckDist`, then traverse the
corner without assigning player position. The capture verifier requires one
corner event, target-wall arrival, 10 stable grip ticks, bounded corner step,
and existing clearance checks.

Host real-map result: 50 approach ticks, start gap 51.20, entry gap 3.57,
49.24-unit approach, exactly one corner event, no climb exits, max step 1.914,
waist/head pushout 0.296, model gap 0.500, zero model-clearance violations.
Ares result: `build/baseline-physics-corner-20260925-175629.txt`, ROM SHA-256
`725086e63bd938eef3fe3061a629be1757abd33ecd5d890609ed194d43756547`, build
marker `Sep 25 2026 17:56:28`. It reports one corner event, destination arrival,
10 stable ticks, max step 0.30, 86 clearance samples, zero violations, and raw
Z/L/R all zero. Full host suite: 81/81.

After rebuilding normal ROM, Ares boot capture
`build/baseline-physics-corner-normal-20260925-175821.txt` (SHA-256
`6f58e8621c00721e4de04c775de690489fd93ed7b165b7ef8c1eb6e40bebcd10`, build
marker `Sep 25 2026 17:58:13`) loads the 45-room map, 32 materials, textured
rooms, and stationary render counters; idle reports `z=0`. This proves runtime
corner mechanics and geometry-derived clearance, not the rendered screenshot
or live-controller feel; manual acceptance remains open.

## Camera-angle texture shimmer follow-up (2026-09-25)

The sprite upload path left the deepest precomputed mip level inaccessible:
libdragon's `rdpq_sprite_upload` passes appended-mip count to
`rdpq_mode_mipmap`, whose API expects total levels including base. Both textured
draw paths now restore the mode with `sprite_get_lod_count(sprite)` before
refreshing tiny3d draw flags. `tests/mipmap_upload_contract.py` is the regression
guard. Profile, UV-range, and TMEM-budget smokes pass; visual confirmation still
requires camera rotation over snow/rock in the reloaded ROM. Existing Ares
session remains untouched.

## Validation record (controls-physics-port Inc 5, 2026-09-07)

**Environment:** Ares (paraLLEl-RDP), libdragon preview build, `madeline_cube_rom.z64`
(elf 440408, make test 58/58, climb_bug_probe 20/20). Human controller playtest
still required for the subjective items; the machine-verifiable subset below was
recorded from device captures (`build/raw-baseline-*`):

1. Stop/turn — host verified (skid trigger + rotate-turn smokes); device: `[move]`
   telemetry shows the Skidding state engaging on reverse input. Human feel pass pending.
2. Tap vs held jump — variable jump height (hold_jump + half-gravity) host-verified
   (jump_smoke); device jump arc not yet eyeballed. Human feel pass pending.
3. Coyote + buffer forgiveness — host-verified (coyote/buffer smokes, consume-once
   device buffers, Inc 1). Human feel pass pending.
4. Dash at 3 yaws — dash steering (DashRotateSpeed) + hitstop host-verified;
   device dash capture pending. Human feel pass pending.
5. Ground dash → jump long jump — dash-jump momentum host-verified (dash_jump_smoke).
   Human feel pass pending.
6. Air dash refill on landing — device-consistent (dash refills on ground touch;
   `air_dash_available` in captures). Human feel pass pending.
7. Climb — DEVICE-VERIFIED (Inc 4): `[climb] START` fires at real baked walls with
   Z + into-wall input (`build/raw-baseline-inc4-grab-20260907-192227.log`, 4 START
   lines + sustained `st=C` climb moves); no auto-latch without Z (final boot
   capture: 0 START). WallUp climb movement, lateral-gated corner system, ledge hop,
   mantle probe all host-verified. NOTE: the demake has UNLIMITED climb grip (OG has
   no stamina) — item 7's "exhaustion" wording does not apply; there is no drain.

**Not re-tuned:** no constants were changed during this pass (the freeze is the
point). If any item feels wrong on the controller, record the value here and
re-tune as a NEW increment, not silently.

## Automated Ares physics self-check (2026-09-25)

With `kDebugPhysicsAutoplay` temporarily enabled, the real Forsaken City map
passed one scripted run/brake/reverse/jump/dash/climb/walljump sequence. The
capture is `build/baseline-physics-full-autoplay-20260925-004341.txt` (raw
serial log: `build/raw-baseline-physics-full-autoplay-20260925-004341.txt.log`).
Machine gates reported: run peak 64.0, brake residual 0.0, reverse speed 64.0,
jump rise 24.0, dash travel 37.7; climb target face 5190, 17 sustained climb
ticks, walljump accepted, and 17 wall-bound clearance samples with zero
violations (minimum/required model gap 0.64/0.64). Scripted climb was labeled
`source=autoplay scripted_c=1`; raw controller shoulders were all zero.
The in-climb Ares frame is saved as
`build/physics-autoplay-corner-20260925-004821.png`; later review found this
frame did not establish clearance from both faces during corner easing.
The separate live-controller log `build/ares-wall-clearance-live.log` also
captured raw `z=1` continuously at neutral stick for input-trace ticks 384–463;
later idle samples (tick 101898 onward) show `z=l=r=0`. This confirms the
earlier no-Z climbing report coincided with an Ares stuck-Z mapping; it was
not evidence of an uncommanded software grab.

This verifies deterministic mechanics on device, not subjective feel. Human
controller checks above remain pending; the autoplay flag was restored to
false and the normal ROM rebuilt after capture. Normal-build Ares boot was
then recaptured at `build/baseline-physics-normal-final-20260925-004659.txt`:
BootMapPack succeeded, the measured player envelope loaded, all four idle
`[input]` samples had `z=l=r=0`, and stationary counters remained
`near_batches=56` / `resident=13`.

## Corner model-clearance follow-up (2026-09-25)

The user-reported wall clipping persisted despite the single-face clearance
gate above. A regression fixture reproduced the miss at a 90-degree outer
corner: the new-face-only render offset left the old-face model gap at `-2.11`
units (required `0.64`). Corner easing changes the active wall normal before
body-position interpolation finishes, so clearance must include both anchor
planes. Runtime now stores both corner normals and offsets the static model
against both plane bounds. A fresh real-map Ares autoplay capture reports
`clearance_samples=30`, `clearance_violations=0`, and `model_gap=0.64`.
Subjective visual confirmation still requires reloading the final ROM in the
user's Ares session; the existing session predates that ROM build.

## Ceiling-contact anchor correction (2026-09-25)

OG `Position` is feet-anchored; demake `PlayerState.position` is center-anchored.
The mover had copied OG's `hit.y - 12` target directly into the center, moving
the feet 10–11.25 units too low on ceiling contact. Added a regression to
`move_service_smoke`: from feet y=10 under ceiling y=20, popout must leave feet
at y=8 and cancel upward velocity. Validation: full host suite 67/67 passed;
normal ROM rebuilt. Current user Ares session was not restarted; device capture
and visual confirmation remain pending.

## Camera-wall clipping follow-up (2026-09-25)

The remaining embedded-looking view had a separate camera cause: wall collision
shortened the boom, then an 18-unit minimum-distance correction moved the camera
back through the same wall. The previous smoke only checked distance and
accepted a camera inside the wall. Camera now constrains its smoothed final
position to the player side of the obstruction and lowers the render near plane
while the boom is blocked. The regression fixture now asserts the camera stays
outside the wall over 300 frames, uses a near plane below 5, and leaves free-room
framing unchanged. Validation: full host suite 67/67 passed; normal ROM rebuilt
to `madeline_cube_rom.z64`. Visual confirmation in Ares remains pending because
current user session was not restarted or disturbed.

## OG physics shell / static model clearance (2026-09-25)

Restored gameplay `player_wall_radius` to OG `WallPushoutDist=3`; Madeline's
prepared static mesh is wider, so the renderer now applies independent
wall-plane clearance (0.5 units) in walk, climb, and both faces during corner
easing. This keeps model dimensions from changing movement, grip, or walljump
behavior. Synthetic climb fixtures now start within OG `ClimbCheckDist=4`
instead of relying on an inflated model-sized probe. Validation: host suite
67/67 passed; `compile-rom.sh` rebuilt `madeline_cube_rom.z64`. Visual
confirmation still requires reloading that ROM in Ares; current session remains
untouched.

## Guided, no-teleport climb capture (2026-09-25)

The physics autoplay previously teleported the player to its selected wall,
which validated climb sustain but skipped real approach, wall contact, and
entry. It now walks toward the selected real-map face, attempts grab at the
measured OG `ClimbCheckDist`, climbs until walljump or mantle, and gates the
result on actual displacement, successful entry, sustained climb, and model
clearance. Capture: `build/baseline-physics-guided-climb-20260925-020022.txt`.
Result: PASS; start gap 51.2, 49 approach ticks, entry gap 3.35, 17 climbing
ticks, 80-unit mantle hop, 17 clearance samples, zero violations, minimum gap
0.50. Raw grab telemetry had `z=l=r=0` because input was scripted. This proves
the scripted real-map route, not subjective controller feel or visual
clearance; user Ares session remains untouched.

## Fixed-tick input replay correction (2026-09-25)

The latest normal Ares capture reports about 30 FPS while gameplay simulates at
60 Hz, so one render-frame input snapshot can feed two fixed ticks. Regression
reproduced a copied jump edge being accepted twice: local consumed flags reset
when copied, and `ConsumeJumpPress` ignored failure to consume the shared device
edge. `PlayerInput` now checks authoritative device-edge availability, action
gates transition only when `Consume*Press()` succeeds, and OG `SkidJump` clears
coyote time/restores recorded height. Added sibling-snapshot input coverage and
a two-tick skid-jump regression; both failed before the fix. Verification:
`./tests/run_host_tests.sh` 67/67 and `./compile-rom.sh` succeeded. Ares process
still has not reloaded this ROM, so current visual/controller feel remains
unverified.

The opt-in `kDebugInputRecording` trace now emits every fixed tick with
`frame`, `sub`, `edge` availability, and `used` flags. This groups multiple
ticks from one render frame and shows exactly which tick consumes a buffered
press. It also records normalized stick input, camera action/forward, current
and target-facing XZ vectors, dash/coyote/jump-hold timers, and air-dash state so a
manual sequence can be replayed and diagnosed. Build isolated live-input ROM
with `make physics-input-trace-rom`; it does not enable scripted autoplay or
change `madeline_cube_rom.z64`. Serial logging changes timing, so use this ROM
to diagnose input/state ordering, not to judge feel or frame pacing.

## Sustained climb ground-snap regression (2026-09-25)

Real-map guided replay found a grounded wall entry whose first upward climb
step was erased: `dy=0`, `grounded=1`, `ground_snap_cooldown=0`. OG
`StClimbingUpdate` calls `CancelGroundSnap()` on every held climb tick after
release/jump/dash exits; the demake only armed the cooldown on launches and
mantle. It now refreshes the cooldown before sustained wall movement.
Regression evidence: grounded synthetic wall test rises 0.667 units on next
tick. Forsaken City replay walks from manifest Start to real wall, rests there
with neutral input for 30 ticks (0.000 drift), then grabs while still
grounded; first climb tick passes and mantle completes after 16 climb ticks.
It records 196 waist/head samples with zero collision-shell overlaps and 62
runtime-equivalent rendered-model samples with zero violations (minimum gap
0.500, maximum correction 6.110). Peak ordinary tick displacement: 1.491.
Continued forward input at this cliff edge rotates sideways per OG ledge-avoid
behavior, so the rest phase deliberately releases movement before measuring
drift. `./tests/run_host_tests.sh`: 68/68; fresh
`make madeline_cube_rom.z64` build SHA-256
`bd3c4f38646fccd3c522eb4d2b5edb91c388509ed15979a8c832adccda7e75c4`.
Opt-in Ares autoplay now gates grounded climb entry, first-step rise (≥0.5), and
the snap cooldown armed during that first sustained tick. Its older aggregate
10-tick gate could hide one canceled step; device rerun is pending reload.
Interactive visual/controller acceptance remains open: Ares PID 190708 started
before this ROM build and was left untouched.

## Isolated autoplay ROM (2026-09-25)

`make physics-autoplay-rom` now builds `physics_autoplay.z64` with a separate
`build/physics-autoplay/` object tree. It embeds the same filesystem and sets
`MC_DEBUG_PHYSICS_AUTOPLAY` only for `gameplay_scene.cpp`; normal
`madeline_cube_rom.z64` keeps autoplay disabled and unchanged. Diagnostic ROM
SHA-256: `b5b8a4ff24eb2bb16b86a68933fc17baf8c7c1d76b360315c18546ba3b0f584d`.
It takes over movement input and emits `[phys-test]` results, including grounded
climb entry, first-step rise, and snap cooldown. Do not use it for manual feel
checks. Ares PID 190708 still holds the older ROM; reload either normal ROM for
manual validation or `physics_autoplay.z64` for the scripted gate.

## Grounded ledge-steering parity (2026-09-25)

Audit found `ledge_sweep_dist=4` was declared but never used, despite OG's
grounded Normal movement rotating input toward nearby supported floor at an
edge. Restored the feet-anchored downward support probes, wall-ahead guard,
and bounded ±0.3 rad / 0.1 rad side sweep. Regression now proves edge steering
and proves ordinary supported motion, wall-ahead movement, idle, and roomless
fixtures stay unchanged. The before-fix regression failed; after-fix full host
suite passed 67/67 and `./compile-rom.sh` rebuilt `madeline_cube_rom.z64`.
This addresses grounded edge movement only; it does not prove the user's
wall/camera visual glitch fixed. User's open Ares session still has an older
ROM loaded, so visual and controller acceptance remain pending reload and
re-test.

## Grounded lip step-up migration regression (2026-09-25)

The prior climb-and-zoom plan required walking over a knee-high lip, but its
implementation lived in the deleted parallel `PlayerMotor`; the single mover
had no equivalent. Synthetic Forsaken-style lip repro pinned grounded movement
at `x=-3`. `MoveService::TryStepUp` now steps to supported ground up to 5 units
high, only from grounded contact, with ceiling and secondary-wall rejection.
`move_service_smoke` verifies 5-unit walkover both at mover level and through
real controller input/state/move ticks, plus 6-unit and low-overhang rejection.
Validation: `./tests/run_host_tests.sh` 67/67; fresh ROM build hash
`73fc8f81f43b49e532f9a5d5aae212cca93ad5a6b082a03c95f98e66ec7e80eb`.
Boot telemetry now prints `[build] compiled=...`; capture files add exact ROM
SHA-256 and size in an `[artifact]` header. This ROM's expected boot marker is
`[build] compiled=Sep 25 2026 03:01:07`.
This regression does not establish that Madeline's visible wall/model overlap
in the user's screenshot is fixed. Current Ares process still has an older ROM
loaded; visual acceptance remains pending.

## Ground-snap landing bookkeeping (2026-09-25)

One-tick controller repro on a 4-unit step-down reached the lower floor at
`feet=-4.00` but ended with `grounded=0` and `was_grounded=0`. OG re-runs
`GroundCheck` after `GroundSnap`; the demake had used that check only to copy
the normal, discarding its contact result. The same missing grounded branch
also left `auto_jump` armed, unlike OG's grounded `LateUpdate`. The regression
now asserts snapped feet, grounded/contact flags, coyote refresh, and
`auto_jump` clearing. Focused test and full host suite pass (67/67); normal ROM
build succeeds. ROM SHA-256 is
`b991c51377ac91770ba83c2d69c3c7f636edf06fd4e3de0b287bd70e7636b1dc`.
Latest-Ares visual/controller acceptance remains pending because the live
session still runs the older input-trace build.

## Sweep contact accumulation (2026-09-25)

Regression swept 8 units across a 2-unit `MAT_DEATH` floor strip, then ended on
safe floor at `x=7`; `death_contact` was false because `Popout` cleared it on
each <=2-unit repair step. Per-move death/ceiling flags now clear once at
`SweepMove`/`SweepMoveClimb` entry and accumulate through substeps, step-up,
climb contacts, and ground snap. Regression failed before the fix and passes
afterward; full host suite passes 67/67 and `make madeline_cube_rom.z64`
succeeds. Current ROM SHA-256:
`19db75053a5971a6e83d2f81eed1d6d4a230186d6a283850f6092df8cf887053`.

This closes a contact bookkeeping defect, not the screenshot's visual
wall/model overlap. Interactive Ares PID 190708 started before this build and
still has the older ROM loaded; latest visual acceptance requires user reload.

## Ground overspeed and map boot regressions (2026-09-25)

Ground carry now preserves OG overspeed: aligned speed 100 decays to 99 in one
60 Hz tick; above-max diagonal steering turns at `RotateSpeedAboveMax` and
retains the rotated `targetFacing`. Slope bias uses pre-turn `targetFacing`.
Host suite passes 74/74, including slope and real-map guided-climb routes.

Missing-map boot logs traced to invalid synthetic `__TB_empty` metadata. Fresh
Ares boot now loads all 32 MATMETA slots and textured LVL rooms; telemetry
reports `near_batches=56`, `texture_uploads=9`, `resident=13`, with no MATMETA
or LVL magic failures. ROM SHA-256:
`156ca7b9d286c33339652a9bfc6928acb4b7ebbf182ebb0c15b69bcefb188fb3`.

This confirms boot and renderer initialization, not interactive movement feel
or camera-angle shimmer. Separate fresh-Ares scripted replay passed with
autoplay ROM SHA-256
`dbfa9ecf10e08c8c43328b09cfb93cb526d2b7b35fe89827e4b9150ffa9e2064`:
run/brake/reverse, jump/dash, airborne steer/release, real-wall approach,
grounded climb entry, first climb step, mantle, and model clearance all passed.
The existing interactive Ares session was left untouched; reload normal ROM
for free-form movement, slope, and camera-angle visual checks.

## Climb wall-jump input parity (2026-09-25)

The climb wall-jump had two unexplained profiles: neutral stick launched at
`(horizontal=40, vertical=105)`, while any movement input used OG
`(horizontal=83.2, vertical=90)`. Focused pre-fix repro failed with neutral
`(0,105,40)`. OG `WallJump()` always sets `WallJumpXYSpeed=MaxSpeed*1.3` and
`JumpSpeed`; removed the input-dependent split. `climb_mantle_smoke` now checks
both neutral and up-input cases against the same outward launch. Host suite:
74/74.

The real-map autoplay now wall-jumps after 10 sustained climb ticks rather
than mantling off its short lip first. Ares capture:
`build/physics-autoplay-walljump-vector-gate-20260925.log`; device result
`PASS`, grounded entry gap 3.35, climb 10 ticks, first climb step 0.67,
`walljump_xy=83.2`, `walljump_y=90`, outward speed 83.2, 10 model-clearance
samples, zero violations, minimum gap 0.50. Normal ROM SHA-256
`df143c56d1398da4a2d0d0d77beb2f5f4ff51e6974378462f961ed41de572266`;
autoplay ROM SHA-256
`5154415ca871712725c8ea91fc8b6f24a76c3cc9881f3d31d1928b438e31aa0b`.
This device route tests wall-jump, not mantle; mantle remains covered by host
and earlier scripted device evidence. Manual controller feel is still open.

## DashJump coyote-height and platform-boost parity (2026-09-25)

Source audit found OG `DashJump()` restores recorded `coyoteZ` and consumes
stored platform velocity after its dash momentum and XY boost. Demake kept the
dash-jump velocity but skipped both operations; an airborne, coyote-valid
dash-jump therefore launched from stale height and lost platform assist. The
shared launch helper now applies OG vertical `[0,180]` and horizontal `300`
carry clamps at Jump, DashJump, SkidJump, WallJump, and mantle-hop sites.
`dash_jump_smoke` reproduces the off-edge case with a deliberately different
current/coyote height and oversized carry, then asserts restored height,
clamped launch velocity, and one-shot consumption. The focused smoke failed
before the fix at the height assertion and passes after. Full host suite passes
75/75; `compile-rom.sh` succeeds. ROM SHA-256:
`65d0ab2a9c8dd0a900dd88f1e096ab73b2b9205a7867ada7cca2024e24864a66`.

Moving-platform carry now has a gameplay writer: grounded `GroundHit.owner_id`
persists rider ownership, and the pre-input surface phase applies displacement
through `SweepMove` while storing OG-clamped launch velocity. `surface_parity_smoke`
covers support ownership, swept ride, and one-shot jump carry. Real-gameplay
moving-platform feel remains unverified because Forsaken City route has no
moving platform in the current smoke path.

`coyote_edge_smoke` now runs the integrated controller off a finite real floor,
waits into the final half of the 0.12 s coyote window, and checks height restore
at the state-phase boundary before movement. Device autoplay still does not
exercise this edge; manual coyote feel remains pending.

## Real-map slope-speed regression (2026-09-25)

Added `real_map_slope_smoke` to the host suite. It scans the staged global CMSH
for a walkable ramp with stable floor normals in both directions, then runs
12 fixed controller ticks uphill and downhill through the real collision
mesh. It compares against the same-tick flat-ground run and rejects blocked or
zero-distance tracks, so a wall beside a ramp cannot masquerade as slower
uphill movement. Current route: face 17, normal `(0, 0.929, 0.369)`,
downhill `73.06`, flat `64.00`, uphill `54.94`; 2 of 75 stable candidates
needed testing before finding a complete two-way route. Host verification only;
manual slope feel still requires current-ROM controller play.

## ROM build-marker freshness (2026-09-25)

Manual logs had `[build] compiled=Sep 25 2026 03:01:07` even though later
gameplay objects and the ROM had been relinked. The marker came from an
unchanged `rom_main.o`, so it could not identify that newer artifact. Make now
refreshes only `rom_main.o` on every ROM build. Verified normal ROM marker TU
recompiled at 08:48 and artifact SHA-256
`e6b561c79589d61849c2b8eaa50d19a56cda415c8cf34a4af25457401f9083da`.

## Fresh physics autoplay and input-trace captures (2026-09-25)

Replaced the stale Ares process with a fresh autoplay ROM built from current
sources. Boot loaded map-pack v2 (45 rooms), global collision (10,618 tris),
all 32 materials, and textured rooms; stationary counters reached
`near_batches=94`, `resident=11`. Autoplay capture
`build/baseline-physics-fix-20260925-094624.txt` has ROM SHA-256
`3da678d50776a804853212689481c1b4aa21d407b8f6450cc49065ccd9758aaf`, marker
`09:46:23`, and terminal `result=PASS`: run 64, brake residual 0, reverse 64,
jump rise 24, dash 37.8, air-steer delta 26.1, release drop 5.3, grounded climb
entry gap 3.35, ten climb ticks, first step 0.67, wall-jump `(83.2,90)`, and
ten model-clearance samples with zero violations. This route did not mantle and
does not prove subjective stick feel.

The capture exposed a verifier defect: it counted `[phys-test]` `climb-target`
and `climb-entry` diagnostics as duplicate terminal results.
`verify_physics_capture.py` now counts only lines beginning
`[phys-test] result=`; its regression test includes both diagnostics. The
captured route now qualifies against the exact ROM SHA.

Separate current input-trace ROM capture recorded 345 unattended ticks with
`stick=(0,0)` and `z=l=r=0`; this confirms clean idle in Ares with no manual
controller input, not real-stick drift. At that point `move_deadzone` remained
zero by the previous user-confirmed feel choice, leaving low-amplitude live
stick response unmeasured. The interactive trace ROM logs raw stick, processed
movement, state, position, and velocity on fixed-tick samples and input/state/
contact transitions.

## OG move-deadzone slop fix (2026-09-25)

Revisited the prior no-deadzone choice after the user asked to fix physics
slop. OG `ControlsConfig::Defaults.Move` uses a `0.35` circular deadzone;
demake `InputSystem` used `0.0`, so tiny nonzero stick noise became active.
The controller normalizes active direction and maps stick magnitudes below
`0.4` to `30%` run speed, making that mismatch especially noticeable.
`move_deadzone` now defaults to `0.35`; it remains configurable for explicit
A/B testing. `input_system_smoke` regression failed before the default changed
and passes after; host suite passes 79/79. Both normal and autoplay ROMs build.

Fresh Ares autoplay capture:
`build/baseline-og-deadzone-20260925-100813.txt`, ROM SHA-256
`c4fa94ea8d936730da83fd6223fa3c65d9f57d7846555eccb261a29c54244a54`, build
marker `10:08:12`. Map-pack, collision, 32-material catalog, textured room,
and render counters loaded; route passed run/brake/reverse, jump/dash, air
control, real-wall approach, grounded climb first step, wall-jump, and model
clearance. `mantle=0`: this route did not test mantle. Autoplay injects input
after `InputSystem`, so this capture verifies no physics regression, not
physical stick response. Live trace now samples every six fixed ticks with
bucketed analog changes and immediate button/state/contact transitions to avoid
per-tick USB-serial flooding. Manual controller feel remains open.

## Independent wall-jump and mantle route gates (2026-09-25)

Audit found the wall-jump route could emit `result=PASS` with `mantle=0`.
Split climb-exit coverage into `walljump` (default) and `mantle` profiles;
each ROM/profile requires its selected exit event, and the capture verifier
checks the profile marker plus event. Run both with:

```sh
bash tools/capture_physics_autoplay.sh 30 walljump walljump
bash tools/capture_physics_autoplay.sh 30 mantle-guided mantle
```

Fresh Ares captures both qualify against current isolated ROMs:

- Wall-jump: `build/baseline-walljump-guided-20260925-102621.txt`, ROM SHA-256
  `d9073254938d39a0e01681ab3d19cee43c5d738d0417582051db02437f832e09`;
  10 climb ticks, wall-jump `(83.2, 90)`, 10 clearance samples, zero violations.
- Mantle: `build/baseline-mantle-guided-20260925-102327.txt`, ROM SHA-256
  `8cfbc0c31e02f39f78d828f64dcffc9490435b232fc2b29cba6e877c41864007`;
  16 climb ticks, mantle event, 16 clearance samples, zero violations.

Both retain run/brake/reverse, jump/dash, air-control, real-wall approach,
grounded climb entry, first-step rise, map/material/renderer health, and raw
shoulder gates. Host suite: 79/79. Scripted route does not replace live
controller feel or visual review.

Normal ROM boot also revalidated after these harness changes:
`build/baseline-latest-normal-boot-20260925-102941.txt`, SHA-256
`fee8b66efcdaea9901536f15e89ebf9e82c8f46df12dd90dac25402f16bdbd2a`.
Map-pack boots all 45 rooms, global collision has 10,618 tris, 32 materials
load, textured rooms report zero discarded faces, stationary render counters
are `near_batches=56 resident=13`, and sampled climb shoulders are zero.

Current live-controller trace ROM rebuilt with the same deadzone and telemetry:
`physics_input_trace.z64`, SHA-256
`325b7251d434ed581ddaa8a12c3b737cf5bab47ebf62e03c4ef619f34a0465d3`.
It leaves movement input live and records raw/deadzoned stick, per-tick input
edges, state, position/velocity, grounded history, quantized support/climb
normals, support/grip face IDs, and ground-snap cooldown. Quantized normal and
face changes emit immediately; periodic sampling remains 10 Hz. Use it for
physical-controller comparison, not scripted route qualification. The current
trace runtime capture is
`build/baseline-physics-trace-support-fields-20260925-123950.txt` (build marker
`12:39:44`, same SHA). Map-pack, 32-material catalog, textured rooms, and
nonzero render counters loaded. Raw log has 45/45 extended support/climb fields
and neutral button samples; player stayed grounded and stationary. This proves
the expanded trace format and clean idle, not physical-controller feel.

## Fresh manual-log diagnosis and idle trace (2026-09-25)

The supplied 03:01 Ares log was stale: room and material files failed to load,
and render counters stayed zero. The later 10:29 normal-ROM log matches then-
current ROM SHA-256 `fee8b66efcdaea9901536f15e89ebf9e82c8f46df12dd90dac25402f16bdbd2a`;
map-pack, collision, all 32 materials, visual rooms, and resident rendering
loaded. Its `[input]` samples report `z=1`, and `[climb] START` attributes
entry to `z=1`, so those climb glitches cannot qualify as physics behavior
under neutral input.

Fresh unattended Ares input-trace capture
`build/baseline-input-health-check-20260925-122139.txt` uses ROM SHA-256
`9abd3a5740ac5ca6e2ead70f4dc43a1f93b37206315775e046d5c9e3b8126c69`.
All 45 sampled `[input-trace]` records show zero raw buttons/triggers and the
player remains grounded and stationary. This proves clean idle in unattended
Ares; it does not establish whether the user's Z press was intentional or a
controller mapping issue. Next live feel capture needs a hands-off baseline
with `z=l=r=0`, then deliberate input transitions.

## Fresh device requalification after trace expansion (2026-09-25)

Latest normal ROM SHA-256 is `f0110431ff4d69f396a55cb91c2e481316f98295108e0f63fd8b4d68bb56114b`.
`build/baseline-physics-resume-normal-boot-20260925-124138.txt` (build marker
`12:39:38`) boots all 45 map-pack rooms, 10,618 collision triangles, all 32
materials, textured visual rooms, and stationary render counters
`near_batches=56 resident=13`; sampled shoulders are all zero.

Fresh Ares physics profiles also qualify against their exact ROM hashes:

- Wall-jump: `build/baseline-physics-resume-walljump-20260925-124038.txt`,
  SHA-256 `328fb3c89043592f5bf110dd7eb277b1c6afd4cf9bd13795474fa318e7579279`.
  Run/brake/reverse, jump/dash, air steering/release, real-wall approach,
  grounded climb entry/first step, wall-jump `(83.2, 90)`, and 10 clearance
  samples pass with zero violations.
- Mantle: `build/baseline-physics-resume-mantle-20260925-124103.txt`,
  SHA-256 `b10a91668a141b1a3975a8b3207d855905ba25e0b1656b90e2cc1c6176247ff7`.
  Same locomotion/air/climb gates pass; mantle completes after 16 climb ticks
  with 16 clearance samples and zero violations.
- Expanded live trace: `build/baseline-physics-trace-support-fields-20260925-123950.txt`,
  SHA-256 `325b7251d434ed581ddaa8a12c3b737cf5bab47ebf62e03c4ef619f34a0465d3`.
  All 45 samples include support normal/surface and climb normal/face fields;
  unattended raw input remains neutral and position stays fixed.

Host suite: 79/79 passed. These scripted/unattended captures prove reproducible
movement and collision gates, not subjective physical-controller feel. The
manual checklist above remains open until the trace ROM receives deliberate
live stick/button input.

## Ares held-Z correlation (2026-09-25)

Read-only inspection of the active Ares `VirtualPad1` settings found
`R-Trigger` bound to gamepad axis 2 low (`0x12dc83106/0/2/Lo`). The supplied
10:29 log reports `z=1` during idle alongside `ar=200`, then logs climb entry
with raw `z=1`. `InputSystem` uses only digital `z/l/r` for climbing and ignores
`analog_r`; the axis binding and raw Z sample are correlated, but current logs
do not prove that the R-trigger axis caused the Z bit. The active unattended
trace session later reported `z=l=r=0` and stayed grounded, so held Z is not
reproduced in that session. Before attributing it to a control binding, inspect
the live Ares mapping that feeds N64 Z and capture deliberate press/release
transitions. Do not mask raw Z in game code based only on the `ar=200` sample.
Live physical-controller feel remains unverified.

The next trace build adds libdragon pad style plus current and stable-held raw
button masks to `[input]`, `[input-trace]`, and `[climb] START`. This separates
device style from the raw Z/R/L bit pattern at the exact transition. Ares
registers separate N64 Z and R inputs; libdragon reports N64 analog R from
digital R but GameCube analog R independently. Source contracts:
[Ares N64 gamepad](https://github.com/ares-emulator/ares/blob/master/ares/n64/controller/gamepad/gamepad.cpp),
[libdragon joypad API](https://github.com/DragonMinded/libdragon/blob/preview/include/joypad.h).

## Fixed-step hitch preservation (2026-09-25)

The supplied Ares trace reported a streaming peak of `121.662 ms`. Before this
fix, `FixedStepAccumulator::BeginFrame()` truncated elapsed time to its five-
tick (`83.3 ms`) work cap, permanently dropping about `38 ms` from a frame of
that size. It now keeps up to ten ticks of bounded backlog, still executes at
most five ticks per render frame, and exposes pending ticks/dropped time in
`[input-trace] sched=(...)`. Regression coverage checks 120 ms catch-up, the
long-stall cap/drop count, and invalid deltas. Final host suite passes 79/79
with all new assertions. Normal ROM SHA-256
`2a745f8e821feb124407859922d432362afb620ef9bfd29656827748628aa86c`;
trace ROM SHA-256 `78beb872bd6a9574a6ffe00185da2f65fba9feb214e0eed814d7ba2034d7ca65`.
The open Ares process still has the prior trace ROM loaded, so hitch telemetry
and live controller feel on these artifacts remain unverified.

## Fresh trace and route requalification (2026-09-25)

`./tests/run_host_tests.sh` passes 79/79; `git diff --check` is clean. Capture
filter regression now requires `tools/capture_baseline.sh` to retain
`[input-trace]` records, so scheduler and raw-mask evidence survives capture.

Fresh normal ROM: SHA-256
`5588babc1194eccd979105fc693d91b1a0288ed8b074eb79a3c158d1c6149b66`, build
marker `Sep 25 2026 13:23:07`, capture
`build/baseline-physics-current-normal-20260925-132546.txt`. Map-pack boots 45
cells, global collision loads 10,618 triangles, all 32 materials and visual
rooms load, and stationary counters report `near_batches=56 resident=13`.

Fresh interactive trace ROM: `physics_input_trace.z64`, SHA-256
`c125799c4a2f9e1f7ddcacf452ca24418c7b223865925ecc2356211853781605`, build
marker `Sep 25 2026 13:23:08`, unattended capture
`build/baseline-physics-input-trace-current-20260925-132723.txt`. All 131/131
samples show N64 style (`style1`), current/held button masks zero, and neutral
raw buttons/triggers; player stays grounded and stationary. Trace now preserves
frame/substep grouping, pending/dropped scheduler time, pad style and masks,
stick, action edges/consumption, movement state, support and climb contacts.
One startup hitch measured 283.3 ms: scheduler ran its 5-tick cap, retained 5
pending ticks, and reported 116.7 ms dropped beyond its bounded accumulator.
Steady samples returned to 33.3 ms frames. This exposes and bounds the stall;
it does not eliminate a real prolonged frame hitch.

Fresh isolated Ares route gates pass against exact ROM identities:

- Wall-jump ROM SHA-256
  `8009168981c56b64b6644582f2c9e4b4e8d4921eb17b0df38c7e5570183dd7a1`;
  capture `build/baseline-physics-after-telemetry-20260925-132414.txt`;
  air-steer delta 26.1, air-release drop 5.3, run/brake/reverse 64/0/64,
  jump rise 24, dash travel 37.8, real-wall approach 49 ticks, grounded climb,
  wall-jump (83.2, 90), 10 clearance samples and zero violations.
- Mantle ROM SHA-256
  `c603a31f2afc71efd2eaa23f5ddf7958f99aa15135e329ee0ea88e6c31b561cb`;
  capture `build/baseline-physics-after-telemetry-20260925-132454.txt`;
  same movement gates, grounded climb and 16-tick mantle, 16 clearance samples
  and zero violations.

These machine gates and real-map fixtures qualify deterministic physics paths,
not subjective live-controller feel. The user's supplied 10:29 log now loads
world textures, unlike the earlier 03:01 stale ROM, but still reports `z=1`
at idle and scripted climb starts. New trace ROM logs style plus raw
current/held masks to distinguish physical Z from stale/button mapping. Manual
acceptance remains open: reload `physics_input_trace.z64`, verify neutral
`pad=(style1,current0000,held0000)` and `z=l=r=0`, then run/release/reverse,
tap/hold A, dash, approach a wall, hold/release Z. Preserve `[input-trace]`,
`[climb] START`, and `[move]` lines for diagnosis. Ares captures above were
unattended; no live input was recorded.

## Current real-map device requalification + history boundary (2026-09-25)

Fresh isolated Ares captures against the current working tree pass all three
real-map routes. They are independent scripted profiles, not live-controller
evidence:

- Wall-jump ROM SHA-256
  `442ccc3fe6cfb7a014a6bf808a4c6caee87d03a5dcaaa9829ef9b91e62dfceba`, capture
  `build/baseline-current-walljump-20260925-210342.txt`: grounded wall entry,
  first climb step +0.67, wall-jump `(83.2, 90)`, 10 clearance samples, zero
  violations.
- Mantle ROM SHA-256
  `5394cbb5a32f4ede3bd94c856cf45d95a041ed5ae037f57eac3cb31d042a3230`, capture
  `build/baseline-current-mantle-20260925-210425.txt`: grounded wall entry,
  first climb step +0.67, 16 climb ticks to mantle, zero violations in 16
  clearance samples.
- Corner ROM SHA-256
  `fa83babd60c6539ac5acdbf92709b24d900500a93550e7b3e3f8d6b73e9df17c`, capture
  `build/baseline-current-corner-20260925-210503.txt`: one corner adoption,
  destination arrival, 10 stable grip ticks, max corner step 0.30, zero
  violations in 86 clearance samples.

All three captures load the current 45-cell map, 10,618-triangle collision
mesh, 32-material catalog, textured rooms, and nonzero resident rendering.
They prove these guided physics routes do not reproduce the reported clip or
auto-grab under neutral scripted shoulders; they do not prove visual/manual
acceptance.

History does not identify a known-good climbing commit yet. `8ea095c` and
`ab1ad1a` are the plan's good-era controls comparison; that A/B result does not
qualify the later wall-climb implementation. Git history places the unified
climb query/OG entry/corner/mantle work in `59a4ba5`–`7726ed6`, then moves climb
onto the single mover in `2eb5ed4`; later fixes in `9e56439` target input,
climb collision, and climb feel. The user's supplied 16:23 log is the clearest
interactive success in this chat: idle has `z=0`, explicit `z=1` starts a
grounded climb, state becomes `C` with wall contact, and a mantle is logged.
It is not a clean visual acceptance because that same run has rapid cell
transitions and no user confirmation that the glitch disappeared. Therefore
do not call any historical commit the last-good visual/feel version without a
same-route A/B or retained ROM capture.

## Live no-Z reproduction (2026-09-25)

The current trace ROM (`physics_input_trace.z64`, SHA-256
`eaa6925011db361d15b9d6c231deca32577b09b67f0a662df2e7e0e5f46f8bd8`) was
reopened in Ares and captured to
`build/interactive-physics-input-trace-followup-20260925.log`. Boot was neutral
(`current0000`, `held0000`, `z=0`). At frame 276 the raw current mask became
`0x2000` while held was still zero; next frame libdragon reported
`held=0x2000`, `z=1`. User explicitly confirmed they did not press Z. The bit
persisted during stick movement, caused climb entry at frame 319 on face 5178,
and led to a mantle at frame 350. This reproduces unintended grab, but evidence
points to an asserted controller/emulator input bit—not a physics-created grab:
`InputSystem` only starts climbing from `held.z`, `held.l`, or `held.r`.

Official Ares N64 input wiring maps virtual `R-Trigger` to N64 `Z`
([source](https://raw.githubusercontent.com/ares-emulator/ares/master/desktop-ui/emulator/nintendo-64.cpp#L34-L37)). Read-only active settings show `VirtualPad1`
`R-Trigger` bound to physical axis 5 high and button 4. This identifies the
unexpected-Z path; the trace cannot distinguish which of those two physical
bindings asserted it. Clear the stale/incorrect binding or map only the
intended physical Z control, then verify `z=l=r=0` at rest and while moving the
stick/A before rerunning physics acceptance. Do not mask Z in game code: that
would hide the input fault and break intentional climbing. Rendered corner
inspection and manual feel remain open; live trace proves climb/mantle state
transitions, not visual clearance.
