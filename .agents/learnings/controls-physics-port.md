# controls-physics-port: FSM cascades, winding-independent grab gate, and empirical gate validation

When porting OG Celeste64 movement into a dispatcher/state machine, adding gameplay
handlers (spring), or changing the climb grab gate — the following lessons apply
(controls-physics-port plan, 2026-09-07).

## 1. Same-tick transition cascades survive into a state-machine refactor

The pre-FSM hand-switched blocks were NOT clean per-state bodies. In
`player_controller.cpp`, the dash acceptance ran OUTSIDE the state blocks (before
the Dashing/Normal/Skidding wrapper), so a dash pressed while Skidding fired the
dash AND ran the Dashing block in the SAME tick. After splitting into
`St*Update` methods the Skidding handler lost the dash — `gameplay_smoke`
caught it (state stayed Skidding). OG parity: `StSkiddingUpdate` calls
`TryDash()` FIRST (Player.cs:1373), and `StNormalUpdate` calls it after climb
entry. Fix: extract a shared `TryDashAccept` and make the dispatcher's cascade
break-condition state-aware (Skidding→Dashing cascades; Skidding→Normal
cancel/done/jump breaks to the facing rotation only).

- Always diff the OLD ordering of blocks vs the NEW dispatcher on the failing
  smoke, not just the smoke line ("player.movement_state == Dashing").

## 2. A "test gate" can be dead code if the prefilter is tighter

The plan's height-band front-side test (face y-extent must overlap the body
band) was intended to reject overhangs — but `QueryWallsFacePlaneMesh` already
AABB-prefilters candidates to `point.y ± radius`, and the grip radius (4-5) is
TIGHTER than the body band (waist±3/7). Measured on the real baked CMSH: the
height band passes 100% of candidates on BOTH sides — it discriminates nothing.
Empirical probe > theory: `probe_scan`/`probe_band` (host, against
`build/bake-fc-1200/staging/forsyken-city.colmesh`) settled the design in
minutes. A base-grounding test also fails on the real map (real wall bases float
5+ units above their floors — parapets/window bands).

## 3. Winding-independent front-side gate = overhead clearance

The baked collision winding is REVERSED (raw normals point INTO walls), so any
`signed_dist < 0` style gate rejects every real wall (the "grab never fires"
bug, 0 `[climb] START` lines on device). The shipped gate: oriented-normal flip
stays; grip/entry replaces the sd<0 reject with (a) a height band (harmless
documentation) and (b) an overhead-clearance reject — if any solid hangs within
8.0 above the waist probe (`RaycastMeshVertical`), the face is an overhang lip /
the player is under a roof → reject (the "under an overhang from behind"
auto-latch guard). Measured: 0% of real-map wall-adjacent waists have solid
within 6 above, so it rejects nothing legitimate. Device proof: temp injection
capture shows `[climb] START` at real house-cell walls + sustained `st=C` moves;
default boot shows 0 START without Z.

- Probe fixtures: add a `reversed` winding flag to the WALL faces only — the
  synthetic floor MUST keep its winding (GroundCheck needs the upward floor
  normal).
- The popout path (orient_to_point=false) is load-bearing (step-down snap, climb
  anchor) — never touch it in a grab-gate change.

## 4. Device capture with fake input: gated temp steering, then revert

Ares cannot take controller input, and `capture_baseline.sh` doesn't drive the
player. The working pattern (Inc 3 spring, Inc 4 grab): flip
`kDebugTeleportToMapCenter` + `kDebugAutoWalk` to true AND add a temp block gated
on the same flag that (e.g.) parks the player 4 units in front of the nearest
real wall (`QueryWallPushout` radius 30 = near-vertical faces only), faces it,
and fakes the input (climb_held + a camera-relative move computed from the
camera basis: input = {dot(w,right), dot(w,forward)}). After the capture, REVERT
the block and the flags and confirm the final ROM boots baseline-identical (elf
size delta proves the temp code is out).

- Do NOT run `pkill -9 -f '[a]res-emulator...'` in the SAME shell command that
  starts Ares: the launcher's own command line contains "ares-emulator" +
  "madeline_cube_rom", so pkill kills the launch shell (silent no-op). Kill in a
  separate call with a self-match-safe pattern `'[a]res-emulator'`.
