# Climb face preference is not a facing gate

## Symptom

Player is within climb range and presses Z, but side-facing wall grab is
silently rejected. Input may also be judged against a stale or unrelated wall
normal.

## Cause

The wall-query preference vector ranks candidate faces; it does not decide
whether a face is climbable. A separate facing-dot threshold narrows OG's grab
behavior. Reusing the previous contact normal for directional input can also
misclassify movement relative to the selected wall.

## Fix

Query the waist-anchored wall first. Keep the explicit climb-button requirement.
If movement input is non-neutral, compare it against the returned face normal;
neutral input remains valid. Treat a stuck raw shoulder bit as controller
mapping evidence, not a reason to add undocumented physics gates.

## Guardrail

Host-test a side-facing neutral grab and an away-from-wall rejection. For device
claims, verify raw Z/L/R samples and capture current ROM identity.
