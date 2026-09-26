# Climb corner ease must keep destination wall

## Symptom

During a real-map corner turn, Madeline snaps back toward the old face, pops
out repeatedly, exits climb, or cuts through the wall despite synthetic corner
tests passing.

## Cause

The corner ease interpolates between two anchors, so intermediate positions may
temporarily miss both wall probes or remain closer to the old face. Re-running
normal grip-face reacquisition, grip-loss timing, or facing-away release during
that transition can overwrite the adopted face or terminate climb mid-ease. A
full-reach ray may also find a disconnected face whose anchor is too far away
for a continuous ease.

## Fix

Treat the ease as one kinematic handoff: retain the adopted destination face
and grip through the transition, honor explicit climb-button release, and do
not run normal facing/grip-loss gates until easing ends. Reject corner anchors
farther than the configured ease reach, and restore the pre-adoption position
as the ease origin so adoption itself cannot teleport the player.

## Guardrail

Keep a replay over a real baked-map edge, not only a synthetic 90-degree fixture.
Assert one corner event, destination-face retention, no climb exits, bounded
per-tick displacement, waist pushout, and rendered-model clearance against both
planes. Current route: `tests/real_map_corner_traverse_smoke.cpp`.
