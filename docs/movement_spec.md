# Movement Spec

This controller is spec-first rather than source-parity-first. `+Y` is up; travel is on the XZ plane; all horizontal intent is projected through the active camera basis.

## Tuned profile

| Value | Current value |
| --- | ---: |
| run max speed | `6.4` |
| ground acceleration / deceleration | `120 / 180` |
| air acceleration / turn acceleration | `24 / 36` |
| jump speed | `9.0` |
| rise / apex / fall gravity | `42 / 84 / 96` |
| jump cut multiplier | `0.5` |
| coyote time | `0.15s` |
| jump buffer | `0.10s` |
| dash speed | `14.0` |
| dash hitstop / active window | `0.05s / 0.18s` |
| stamina max | `110` |

## State model

Physical locomotion uses `IDLE`, `RUN`, `JUMP`, `DASH`, `CLIMB`, and `FALL`.

- Grounded release stops almost immediately; ground turning is intentionally short, not Mario-like.
- Air steering curves velocity instead of deleting momentum.
- Jump height is variable: early release cuts upward velocity once; rise, apex, and fall use distinct gravity bands.
- Dash aims freely through camera-relative analog input, freezes movement briefly, then travels gravity-free.
- Climb consumes stamina at different rates for hold, upward movement, and climb jump; exhaustion blocks re-entry until grounded refill.

## Known non-goals

- No 8-way dash quantization while the demake remains analog-first.
- No physical skid state; skid can return later as animation/VFX.
- No retuning against OG source traces unless a collision regression is under investigation.
