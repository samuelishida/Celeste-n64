# OG port gap matrix

The original game is not hiding a separate physics engine behind `Player.cs`; it is leaning on a compact runtime substrate. This ledger names the pieces we need before future source ports become straightforward translation work.

## Trace contract

Movement trace fixtures are JSON objects with:

- `name`
- `source` (`og` or `port`)
- `dt`
- `tolerance` (`position`, `velocity`, `scalar`)
- `frames[]`, each with `input`, `position`, `velocity`, `grounded`, and optional tags

Default parity tolerance unless a fixture says otherwise:

- position: `0.05`
- velocity: `0.05`
- scalar timers: `0.01`

## Dependency ledger

| Source dependency | OG evidence | C++ status | Gap |
| --- | --- | --- | --- |
| buffered virtual buttons | `Controls.cs`, historical Foster `VirtualButton` | **done (Inc 1)** | `InputSystem` owns `VirtualButtonState` (0.10 s device buffer + consume-once); controller's internal buffers deleted |
| deadzoned virtual sticks | `Controls.cs`, Foster `VirtualStick` | **done (Inc 1)** | `VirtualStickState` + circular deadzone; `move_deadzone` config field (default 0.0, OG 0.35 behind a flag) |
| angle / approach / interval helpers | Foster `Calc`, local player helpers | partial | controller carries private clones (out of scope) |
| state machine | `Helpers/StateMachine.cs` | **done (Inc 2)** | `StatePhase` dispatcher over Normal/Dashing/Skidding/Climbing with enter/exit hooks + same-tick transition cascade (enum stays at the 4 locomotion states) |
| actor lifecycle / typed queries | `World.cs` | missing | actors are lightweight structs with bespoke scene wiring (out of scope) |
| pickup / pushout / rider traits | `Interfaces.cs`, `Player.cs` | missing | behaviour is hardcoded per path (out of scope; Spring is wired directly by the scene, Inc 3) |
| actor-backed solids | `Solid.cs`, `World.cs` | partial | query results exist, but ownership lives in fixed `Room` arrays |
| moving-solid rider propagation | `Solid.MoveTo`, `IRidePlatforms` | partial | moving surfaces exist, but not through actor traits (out of scope) |
| climb snap-up / cornering / ledge assist | `Player.cs` | **done for entry + corner system + ledge hop** | entry snap, wallUp climb movement, lateral-gated corner system, ledge hop ported (climb-fix loop + controls-physics-port Inc 4 winding-independent grab gate) |

## Required trace fixtures

- `buffered_jump.json` — press before landing, consume once
- `wall_snap.json` — upward approach snaps into a climbable wall
- `ledge_assist.json` — floor-edge correction preserves supported motion
- `moving_platform_jump.json` — stored platform velocity adds once on takeoff
- `actor_overlap.json` — trait-driven overlap query selects the expected actor
