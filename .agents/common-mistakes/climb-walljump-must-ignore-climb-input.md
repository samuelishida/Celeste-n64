# Climb wall-jump launch must not depend on climb steering input

## Symptom

Wall-jump trajectory changes sharply depending on whether movement stick is
neutral or held while the player climbs.

## Cause

An input-dependent neutral branch selected a separate launch-speed pair. OG
`Player.WallJump()` always sets outward horizontal speed to `MaxSpeed * 1.3`
and vertical speed to `JumpSpeed`; it does not inspect movement input.

## Fix

Use the same outward wall-jump velocity for neutral and directional climb
inputs. Keep facing/launch direction derived from the wall normal.

## Guardrail

Run neutral and up-input cases in `climb_mantle_smoke`; assert equal OG launch
components and outward direction. Real-map autoplay should reach a wall,
sustain climb, issue one wall-jump, and report a terminal result.
