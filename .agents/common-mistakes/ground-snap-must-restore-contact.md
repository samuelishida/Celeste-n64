# Ground snap must restore grounded contact

## Symptom

Madeline's feet snap onto a lower floor, but the same physics tick still reports
`grounded=false`. The next state update applies air control; `auto_jump` can
remain armed, and a snapped death surface may fail to trigger `death_contact`.

## Cause

The mover adjusts center position to the floor and performs a confirming
`GroundCheck`, but callers discard the grounded result. Controller landing
bookkeeping then skips coyote/dash refresh. The snap path can also omit death
material contact. OG `LateUpdate` reassigns `onGround` from `GroundCheck` after
a successful snap and clears `autoJump` on grounded contact.

## Fix

On successful post-snap `GroundCheck`, set `PlayerState::grounded` and the
ground normal and snapped face material flags, including `death_contact`. Run
shared grounded bookkeeping afterward, including clearing `auto_jump`,
refreshing coyote height/time, and dash refill eligibility.

## Guardrail

Keep a one-tick controller fixture that walks off a ledge onto a floor within
the 5-unit snap range. Assert feet position, `grounded`, `was_grounded`,
coyote refresh, `auto_jump` clearing, and death-material propagation in the
same tick.
