# Ground and air movement use different analog-speed rules

## Symptom

Partial stick feels too slow in air, or releasing the stick leaves horizontal
air velocity unchanged.

## Cause

OG normalizes `RelativeMoveInput` and air steering targets full `MaxSpeed`; the
`AnalogMagnitude` multiplier applies to grounded movement only. OG also applies
`AirFrictionMult=0.1` to horizontal velocity with no air input. Reusing the
grounded target for air or omitting idle-air friction changes both behaviors.

## Fix

Keep grounded analog scaling and airborne direction targeting separate. Apply
air friction only when movement input is idle. Preserve OG update order:
horizontal movement, coyote jump, directional wall-jump, then gravity; a new
wall-jump launch must not be attenuated by same-tick air friction.

## Guardrail

Test grounded partial stick, airborne partial stick, and no-input air decay
separately. Keep an exact launch-velocity assertion for directional
wall-jumps.

## Grounded overspeed carry and steering

Grounded motion has a separate parity rule: save slope-adjusted `trueMax`
before analog-stick scaling. When speed remains above `trueMax` and input
aligns with velocity, use `PastMaxDeccel`; above `trueMax`, rotate
`targetFacing` at `RotateSpeedAboveMax`. Do not overwrite the rotated facing
with raw input later in the same update.

Guard with a one-tick aligned carry case (`100 -> 99` at 60 Hz) and an
above-max diagonal-turn case. Keep a slope test proving max-speed bias uses
pre-turn `targetFacing`, not ledge-adjusted input.

## Air-dash launch is 3D

Air dashes are not horizontal copies of grounded dashes. OG `SetDashSpeed`
uses `Normalize((direction.x, 0.4, direction.z)) * DashSpeed` in air, but
`(direction.x, 0, direction.z) * DashSpeed` on ground. Apply that vector when
the dash is accepted; the state-transition cascade must not erase it before
the first mover tick. Local hitstop may freeze subsequent ticks, but must not
consume active dash duration. Air-dash expiry scales all three velocity
components by `DashEndSpeedMult`.

Guard grounded and airborne launch vectors separately, plus the active vector
after hitstop and full-3D exit scaling. Do not let a test assert zero launch
velocity merely because the local hitstop pauses later movement.
