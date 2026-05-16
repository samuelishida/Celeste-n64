# Movement Spec

Prototype values now track the Celeste64 controller shape more closely while staying in the smaller graybox unit scale.

## Coordinate assumptions

- `+Y` is up.
- Ground movement happens on the `XZ` plane.
- Units are arbitrary engine units until the first imported level exists.

## Player

| Value | Current value |
| --- | ---: |
| run speed | `6.4` |
| acceleration | `50.0` |
| friction | `80.0` |
| gravity | `60.0` |
| jump speed | `9.0` |
| dash speed | `14.0` |
| dash duration | `0.20s` |
| respawn fall height | `-20.0` |

## Inputs

- analog stick controls desired movement relative to the orbit camera
- `A` requests jump
- `B` requests dash
- C-left / C-right orbit the camera
- C-up / C-down adjust camera distance

## Current behavior

- analog magnitude scales grounded run speed
- grounded movement rotates toward the desired direction and enters a skid on hard reversals
- jump sustain uses the source game's hold window plus half gravity near the apex
- coyote time, jump buffering, grounded dash, airborne dash lift, dash end slowdown, independent wall jump probes, and a first dedicated climb state are active
- movement now resolves through a sweep/popout-style motor with late contact refresh instead of direct integration plus one coarse room pass
- floor/ceiling/wall ray queries are shared by the player motor and camera obstruction handling
- touching ground restores the dash after the dash-reset cooldown
- falling below the kill plane teleports the player to the checkpoint and clears transient movement state
