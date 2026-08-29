# Ground-snap cooldown must be armed at every takeoff (CancelGroundSnap parity)

## Context
og-mover-overhaul review pass (2026-08-29) on the single-mover MoveService PR.

## Mistake
`MoveService::GroundSnap` gated on `contact.ground_snap_cooldown_remaining > 0`,
but NOTHING ever wrote a positive value — the only writer was the
`Approach(...,0)` decay in TimerInputPhase. The gate was permanently open, so
every jump takeoff was instantly snap-pulled back to the floor (rise under the
5-unit ray) until gravity outran the 5-unit ray. Host tests missed it because
their jump asserts read velocity and most tick via `Step(nullptr)`, which skips
MovePhase. OG arms the same cooldown via `CancelGroundSnap()` at EIGHT sites
(Player.cs:722-723, callers at :742 Jump, :758 WallJump, :775, :915
WallJumpCheck, :939 TryDash, :1473 climb) — the demake ported the gate but
dropped every setter.

## Rule
Any time you add a gated timer, grep BOTH halves: the reader (gate) and the
writer. A cooldown whose only writer is its own decay is a dead gate — and if
the gate protects against a takeoff-eating snap, the missing setter is a live
gameplay bug, not a latent one. Same audit pattern: who SETS `t_ground_snap
_cooldown` in OG (8 CancelGroundSnap sites) must be ported with the gate that
reads it.

## Fix shape
`MovementConfig::ground_snap_cooldown = 0.10f`; controller sets
`contact.ground_snap_cooldown_remaining = config_.ground_snap_cooldown` at
every OG `CancelGroundSnap` site (Jump, dash-jump, walljump ×2, dash accept,
skid-jump, mantle).
