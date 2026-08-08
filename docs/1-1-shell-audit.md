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

## Numeric fixtures

The text-only fixture has been replaced by numeric values in
`tests/fixtures/1-1-shell-probes.json`. Each probe carries:

- `origin` - ray start in game space
- `direction` - unit ray direction
- `max_t` - maximum ray distance (world units)
- `expect` - `hit` (a wall must seal this shell edge) or `miss` (no floor may
  overrun past this shell edge)
- `expected_distance` / `expected_normal` / `expected_material` - verified on
  hit, within `distance_tolerance` / `normal_dot_tolerance`
- `note` - the original audit note for the probe

Values are derived from the current baked colmesh
(`filesystem/lvl/1-1.colmesh`) and committed — no test may capture them on
first run. `tests/shell_probe_test.cpp` validates these numerically against the
host `CollMesh` query path.
