# Movement Spec

This controller matches OG Celeste64 values (Y-up, same numeric values as OG — no rescaling). `+Y` is up; travel is on the XZ plane; all horizontal intent is projected through the active camera basis.

## Tuned profile (Y-up OG values)

| Value | Current value | OG source |
| --- | ---: | --- |
| run max speed | `64.0` | MaxSpeed |
| ground acceleration / deceleration | `500 / 800` | Acceleration / Friction |
| past max deceleration | `60` | PastMaxDeccel |
| air acceleration (base, scaled by mult) | `500` | Acceleration * AirMult |
| air accel mult min/max | `0.5 / 1.0` | AirAccelMultMin/Max |
| rotate threshold | `12.8` | RotateThreshold (MaxSpeed*0.2) |
| rotate speed / above max | `9.42 / 3.77 rad/s` | RotateSpeed / AboveMax |
| jump speed | `90.0` | JumpSpeed |
| jump hold time | `0.10s` | JumpHoldTime |
| jump XY boost | `10.0` | JumpXYBoost |
| gravity | `600` | Gravity (single value, mult 0.5 near apex when jump held) |
| half-gravity threshold | `100.0` | HalfGravThreshold |
| max fall speed | `-120.0` | MaxFall |
| coyote time | `0.12s` | CoyoteTime |
| jump buffer | `0.08s` | JumpBufferTime |
| dash speed | `140.0` | DashSpeed |
| dash hitstop / active window | `0.02s / 0.20s` | HitStun / DashTime |
| dash end speed mult | `0.75` | DashEndSpeedMult |
| dash reset cooldown | `0.20s` | DashResetCooldown |
| dash cooldown | `0.10s` | DashCooldown |
| dash rotate speed | `1.88 rad/s` | DashRotateSpeed |
| dash jump speed / hold / time | `40 / 20 / 0.3s` | DashJump* |
| dash jump XY boost | `16.0` | DashJumpXYBoost |
| dash air up component | `0.4` | Air dash Y (normalized) |
| wall jump XY speed | `83.2` | WallJumpXYSpeed (MaxSpeed*1.3) |
| wall jump Y speed | `90.0` | JumpSpeed |
| climb speed | `40.0` | ClimbSpeed |
| climb hop up / forward | `80 / 40` | ClimbHop* |
| mantle input lockout / regrab cooldown | `0.25s / 0.30s` | ClimbHopNoMoveTime / tClimbCooldown |

## State model

Physical locomotion uses `IDLE`, `RUN`, `JUMP`, `DASH`, `CLIMB`, and `FALL`.

- Grounded: rotation-based turning above `rotate_threshold` (12.8); simple Approach below. Friciton stops fast.
- Air steering: curves velocity toward input direction. Past-max decel (60) when above max speed. Air mult scaled by input-velocity dot.
- Jump: variable height via 0.1s hold at `jump_speed` (90). Half-gravity (0.5×) when button held and |vel| < 100. Early release = shorter jump.
- Coyote: 0.12s grace after leaving edge. Snap Y to `coyote_height` on coyote jump.
- Jump buffer: 0.08s pre-landing window triggers jump instantly.
- Dash: aim through camera-relative input, 0.02s hitstop, 0.2s gravity-free travel at 140. Air dash adds 0.4 Y component. End: velocity *= 0.75 if airborne. Dash-jump: lower jump (40) with longer hold (0.3s at 20).
- Camera: from-style orbit (right stick, 4 rad/s), 3-tier zoom (30-110 distance), exponential follow (0.01^dt), wall raycast, dynamic FOV (1.0-1.2x at high speed). **Zoom deviation:** the demake sweeps normalized distance at `1.8/s` (`CameraConfig::zoom_rate`) and settles detents at `1.0/s` (`zoom_snap_speed`), versus OG's `1.0/s` (`Player.cs:283`). Rationale: the N64 stick/C-button medium lacks a mouse wheel's fine control, so 1.8x/2x faster zoom/snap reads as responsive without skipping detents. **Position-follow gate:** `target_distance` alone is not enough — the camera-position follow (`camera_follow_decay=0.01`, ~7%/frame) would otherwise lag the actual zoom by ~2-3s. While the player is actively zooming the follow decay tightens to `0.0001` so the camera chases the zoomed-in spot immediately; when zoom stops it eases back on the normal decay. Orbit/follow/FOV rates otherwise stay OG-exact.

## Climb model (OG Celeste64 parity)

Unlimited grip: there is **no stamina** in Celeste64 or in this demake. Climbing
is limited only by holding the Climb button (Z/L/R), touching a wall, and
facing it.

- **All solid walls are climbable** — no material gate at runtime (the
  `MAT_CLIMBABLE` collmesh bit remains reserved for future bake-side use).
- Entry: Climb held + wall contact + facing-dot ≥ 0.5 + stick neutral OR pushing
  into the wall (dot ≤ -0.5). Grounded entry allowed (OG-faithful).
- Grab zeroes all velocity; climbing is gravity-free kinematic movement on the
  wall plane: raw stick Y = up/down at `climb_speed` (down canceled while
  grounded); camera-relative horizontal intent projected onto the wall tangent.
- Release: button released / waist-level grip probe lost / faced away. No
  cooldown on release (only the mantle sets one) so hanging at a lip cannot
  pump into fall/regrab cycles.
- Climb walljump: SET velocity away from the wall normal — directed 83.2 XY /
  90 Y; neutral 40 XY / 105 Y (repo-local neutral split, kept for feel).
- Ledge-hop mantle: holding up while the lip probe misses → ballistic hop
  (-wall_normal × 40 forward, 80 up), 0.25 s move-input lockout, 0.3 s re-grab
  cooldown. Probe bands key off the feet (`climb_grip_probe_center_offset`,
  `climb_ledge_probe_center_offset`) exactly like OG's waist-relative checks.
- Ground acceptance: raycast-path floors require `normal.y >= 0.35`; sphere
  sweeps require `>= 0.70`, or `>= 0.35` plus a downward-raycast floor
  confirmation (see wall-contact table below) — walls can never register as
  ground, and mid-wall diagonal sweep hits are rejected.

## Wall-contact model (OG plane-based parity, wall-climb-overhaul)

| Aspect | Value |
|---|---|
| Wall normal source | Face-plane normal from the owning triangle (`QueryWallsFacePlane`), oriented toward the query point; stable across seams/corners (no sphere-closest-point diagonal) |
| Wall gate | `|plane_normal.y| < 0.35` (OG `ClimbNormalCheck`) — only near-vertical faces are walls; slopes are never grab surfaces |
| Grip identity | Persistent — climb re-acquires the stored `grip_face_id` (static-collmesh triangle) rather than the nearest-any-wall normal; updates only on a genuine wall change (corner traversal) |
| Positional pushout | Full-3D (`plane_normal` × pushout), per-substep vertical cap ±6 units — yields free step-up over ankle/knee-high ledges |
| Velocity removal | **Impact-only**: on contact-edge substeps or when motion drives >1 unit deeper into the wall, remove the into-normal component once against the true face plane. Resting contact never edits velocity — no continuous fall damping |
| Wall-contact flag | Sourced from the motor body-center sweep **OR** the waist plane-probe (`PlayerMotorConfig::wall_probe_center_offset`), so motor and climb controller agree on grip |
| Fall speed at walls | Preserved exactly (tangential), so brushing a seam no longer eats `vy`; slide is full fall speed (no separate slide state, OG-faithful) |
| Floor sweep acceptance | Sphere-sweep floor requires `normal.y >= 0.70` outright, OR `>= 0.35` plus a downward-raycast floor confirmation at the sweep point. Raycast-path floor gate stays `0.35`. Kills mid-wall diagonal "grounded" hooks |
| One-way / death | Handled at the face layer as before (pass-through from below, death material) |

The seam-pump (43 `[climb] START`/tick) is fixed by the plane-based face normals
+ persistent grip identity: seams no longer produce a diagonal normal that
flips the release check or eats fall speed.

## Known non-goals

- No 8-way dash quantization while the demake remains analog-first.
- No physical skid state; skid can return later as animation/VFX.
- Controller-local hitstop (0.02s), not OG world-level freeze.