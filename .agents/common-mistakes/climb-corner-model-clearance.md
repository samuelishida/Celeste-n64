# Climb corner model clearance must cover both walls

## Symptom

The player mesh clips into a wall during the eased turn around a corner even
though the climb telemetry reports a passing model-clearance gap.

## Cause

Corner adoption switches the active wall normal immediately, while the body
position continues to interpolate from the old wall anchor to the new one. A
renderer that checks only the active/new plane can leave the mesh inside the
old perpendicular plane. A single-plane telemetry gate can therefore pass
while the rendered model is still occluded.

## Fix

Retain both corner-anchor normals for the duration of the ease. Compute one
horizontal translation satisfying model-clearance constraints against both
planes, and validate both gaps in the corner regression and device telemetry.

## Guardrail

Keep a 90-degree two-wall fixture that proves the new-plane-only offset clips
the old plane, then asserts the dual-plane offset clears both. A real-map Ares
corner capture must include samples from both planes; a passing single-plane
gap is insufficient.
