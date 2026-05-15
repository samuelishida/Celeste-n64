# Movement Spec

Initial placeholder values for Milestone 0. These are deliberately simple and should be tuned by feel once the ROM is running.

## Coordinate assumptions

- `+Y` is up.
- Ground movement happens on the `XZ` plane.
- Units are arbitrary engine units until the first imported level exists.

## Player

| Value | Initial value |
| --- | ---: |
| run speed | `6.0` |
| ground acceleration | `36.0` |
| air acceleration | `18.0` |
| ground friction | `28.0` |
| gravity | `24.0` |
| jump speed | `9.0` |
| dash speed | `14.0` |
| dash duration | `0.16s` |
| respawn fall height | `-20.0` |

## Inputs

- analog stick controls desired `XZ` move direction
- `A` requests jump
- `B` requests dash

## Milestone 0 behavior

- The player can jump only while grounded.
- The player gets one dash while airborne.
- Touching ground restores the air dash.
- Dash uses the current move direction when present; otherwise it uses the last non-zero facing direction.
- Falling below the kill plane teleports the player to the checkpoint and clears velocity.

## Deferred to Milestone 1

- coyote time
- jump buffering
- variable jump height
- wall grab / wall climb
- dash refill effects

