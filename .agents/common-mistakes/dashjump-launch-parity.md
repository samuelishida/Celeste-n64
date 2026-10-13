# DashJump must restore coyote height and consume platform carry

## Symptom

A dash-jump near a ledge launches from an airborne height instead of the last
supported height, or loses the boost from a moving platform.

## Cause

OG `DashJump()` writes `Position.Z = coyoteZ`, then calls
`AddPlatformVelocity(false)` after preserving dash momentum and applying its
horizontal boost. The demake launch helper can bypass either operation when
ported independently from ordinary `Jump()`.

## Fix

Before DashJump launch, restore `PlayerState.contact.coyote_height`. After
setting the launch vector, consume stored platform velocity once, clamping
vertical carry to `[0,180]` and horizontal magnitude to `300`, matching OG.
Audit sibling impulse sites (SkidJump, both WallJumps, mantle hop) against each
OG `AddPlatformVelocity` call.

## Guardrail

Host-test a state that is airborne but still coyote-valid, with current height
different from recorded height and oversized stored platform velocity. Assert
restored height, clamped launch vector, cleared carry timer, and no second
same-tick Normal update. This proves controller parity; separately verify the
runtime moving-surface path can populate the stored carry state.
