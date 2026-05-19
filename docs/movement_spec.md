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
| stamina max | `110.0` | StaminaMax |
| stamina hold drain | `10/s` | Per second holding |
| stamina up drain | `20/s` | Per second climbing up |
| stamina jump cost | `25` | Flat cost per climb-jump |
| stamina refill | instant on ground | Refills to max on landing |
| climb exhaustion | blocks re-entry | Until grounded refill |

## State model

Physical locomotion uses `IDLE`, `RUN`, `JUMP`, `DASH`, `CLIMB`, and `FALL`.

- Grounded: rotation-based turning above `rotate_threshold` (12.8); simple Approach below. Friciton stops fast.
- Air steering: curves velocity toward input direction. Past-max decel (60) when above max speed. Air mult scaled by input-velocity dot.
- Jump: variable height via 0.1s hold at `jump_speed` (90). Half-gravity (0.5×) when button held and |vel| < 100. Early release = shorter jump.
- Coyote: 0.12s grace after leaving edge. Snap Y to `coyote_height` on coyote jump.
- Jump buffer: 0.08s pre-landing window triggers jump instantly.
- Dash: aim through camera-relative input, 0.02s hitstop, 0.2s gravity-free travel at 140. Air dash adds 0.4 Y component. End: velocity *= 0.75 if airborne. Dash-jump: lower jump (40) with longer hold (0.3s at 20).
- Climb: consumes stamina. Hold=10/s, up=20/s, jump=25 flat. Exhaustion at 0 blocks re-entry until grounded. Wall jump launches away from wall normal.
- Camera: OG-style orbit (right stick, 4 rad/s), 3-tier zoom (30-110 distance), exponential follow (0.01^dt), wall raycast, dynamic FOV (1.0-1.2x at high speed).

## Known non-goals

- No 8-way dash quantization while the demake remains analog-first.
- No physical skid state; skid can return later as animation/VFX.
- Controller-local hitstop (0.02s), not OG world-level freeze.