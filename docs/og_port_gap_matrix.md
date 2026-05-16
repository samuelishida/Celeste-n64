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
| buffered virtual buttons | `Controls.cs`, historical Foster `VirtualButton` | partial | jump buffer lives in player state; no reusable consume-once input layer |
| deadzoned virtual sticks | `Controls.cs`, Foster `VirtualStick` | partial | raw N64 input exists; no reusable virtual stick abstraction |
| angle / approach / interval helpers | Foster `Calc`, local player helpers | partial | controller carries private clones |
| state machine | `Helpers/StateMachine.cs` | missing | player states are hand-switched |
| actor lifecycle / typed queries | `World.cs` | missing | actors are lightweight structs with bespoke scene wiring |
| pickup / pushout / rider traits | `Interfaces.cs`, `Player.cs` | missing | behaviour is hardcoded per path |
| actor-backed solids | `Solid.cs`, `World.cs` | partial | query results exist, but ownership lives in fixed `Room` arrays |
| moving-solid rider propagation | `Solid.MoveTo`, `IRidePlatforms` | partial | moving surfaces exist, but not through actor traits |
| climb snap-up / cornering / ledge assist | `Player.cs` | partial | several behaviours remain unported |

## Required trace fixtures

- `buffered_jump.json` — press before landing, consume once
- `wall_snap.json` — upward approach snaps into a climbable wall
- `ledge_assist.json` — floor-edge correction preserves supported motion
- `moving_platform_jump.json` — stored platform velocity adds once on takeoff
- `actor_overlap.json` — trait-driven overlap query selects the expected actor
