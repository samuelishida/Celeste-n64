# 1-1 Shell Audit

## Why this exists

The current `1-1` import still needs a human-readable audit trail for the room
shell. The screenshots show the two failure modes we care about first:

- an outer wall that appears to be missing on one side
- floor triangles that visually continue beyond the intended enclosure

This note names the probe cases the host test will print while the bake is being
made faithful.

## Current source observations

- `assets/og_converted/maps/1-1.map` contains one `worldspawn` with 13 brushes.
- The import still includes legacy OG classes such as `Decoration`,
  `SpikeBlock`, `TrafficBlock`, `DeathBlock`, `func_group`, and `Cassette`.
- `tests/fixtures/1-1.manifest` now mirrors the shell-only bake and should stay
  in lockstep with `filesystem/lvl/1-1.manifest` as the room contract evolves.

## Named shell probes

- `west_outer_wall_gap` - probe the side that should close the room shell.
- `east_floor_overrun` - probe the side where the floor visually spills past
  the enclosure.
- `north_outer_wall_gap` - probe the opposite perimeter wall.
- `south_floor_overrun` - probe the opposite floor edge.

These probe names are intentionally descriptive so the host log can point to the
shell problem before the bake policy is changed.
