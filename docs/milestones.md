# Milestones

## Milestone 0 - Madeline cube ROM

Goal: prove the full loop on N64 with placeholders.

Required:

- flat floating island with static collision
- cube player with analog movement
- third-person follow camera
- jump
- single air dash
- kill plane and respawn point
- one collectible cube

Out of scope:

- final Madeline model
- wall climb
- music
- title screen
- authored level art

**Status: COMPLETE** — booting ROM with all features.

## Milestone 1 - Celeste feel test

Goal: make the movement worth keeping.

Add:

- coyote time
- jump buffer
- dash cooldown/reset tuning
- friction and air control tuning
- camera follow smoothing
- first pass wall grab

**Status: IN PROGRESS** — movement constants in `docs/movement_spec.md`.

## Milestone 2 - One real room

Goal: replace the graybox with a compact challenge space.

Add:

- hand-built mountain test room
- one dash gap
- one climb wall
- one collectible route
- one obvious respawn challenge

**Status: SUPERSEDED** — replaced by whole interconnected map (Forsaken City A-side).

## Milestone 3 - Whole interconnected map (Forsaken City A-side)

Goal: convert and play the entire Forsaken City level as one traversable world.

Add:

- Grid-chunked bake pipeline (`tools/bake_map_pack.py`)
- Map-pack format + binary manifest (`tools/mappack_format.py`)
- Multi-room runtime (`Map` container + active-chunk streaming)
- Chunk transition triggers + player-state carry
- Per-chunk save (strawberry bits, checkpoint room id)
- B-side cassette Push/Pop via SetLevel
- Parity tests, probes, cleanup, docs

**Status: COMPLETE + FIXUP** — all increments 1-7 done; a post-ship regression
(fall-through on boot) was fixed by `.plans/interconnected-map-fixup/`: the
bake's grid partition is now WORLD-XZ (map `(x, −y)` = world `(x, z)`; the
old partition keyed the second axis by map_z = the Quake UP axis, so chunks
never matched the runtime's cell resolution and the player fell through at
boot). `--chunk-size 1200` (was 650) yields 47 chunks for `1.map`; boot uses
the manifest `start_spawn` (the `Start`-named PlayerSpawn); colmesh reuse is
opt-in (`--reuse-colmesh`, default off). ROM builds with
`make bake-forsaken-city && ./compile-rom.sh`.

## Milestone 4 - N64 presentation pass

Goal: make the prototype read as a game instead of a test scene.

Add:

- low-poly player model
- sky/fog/material pass
- compact UI
- jump/dash/collect SFX
- title screen

