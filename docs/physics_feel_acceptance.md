# Physics feel ROM acceptance

Run this on a controller in baked level `first-room` after the host suite is green.

1. Hold run, release stick, then reverse: Madeline should stop/turn within roughly three frames by eye.
2. Compare tap jump vs held jump: tap jump must peak visibly lower.
3. Walk off an edge and jump late; press jump just before landing; both forgiveness windows should be obvious without double-firing.
4. Dash at three camera yaws (forward, 90°, diagonal): aim follows the stick relative to the camera, with a visible impact pause first.
5. Ground dash then jump near the end: long jump travels farther than a run jump.
6. Spend dash in air: it only refills on landing or crystal.
7. Climb until exhausted: idle drains slowly, upward climb drains faster, climb jump spends a chunk, exhaustion forces fall until ground touch.

Record: emulator/hardware, controller, date, and any value that feels wrong before freezing constants.

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
