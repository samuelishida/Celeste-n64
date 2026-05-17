# Acceptance Scenarios

## Scenario 1: Level loads

GIVEN a fresh `make`
WHEN the ROM boots in Ares
THEN serial output shows the baked `1-1` load, six manifest materials, and spawn near `(0.64, 7.68, -2.40)`.

## Scenario 2: First-room derivatives are conditioned

GIVEN the first-room logical material set
WHEN the converter runs
THEN each emitted room material is a `32x32 RGBA16` derivative rather than an oversized source-resolution texture.

## Scenario 3: Level materials are hardware-valid

GIVEN the `1-1` manifest
WHEN `python3 tests/material_budget_smoke.py` runs
THEN every referenced level sprite is reported TMEM-safe before the ROM is launched.

## Scenario 4: Geometry renders textured

GIVEN Inc 3 complete
WHEN the player looks around from the `1-1` spawn
THEN at least two visibly distinct TMEM-safe materials appear, geometry is not uniformly black or white, and sky remains visible outside geometry.

## Scenario 5: Lighting responds

GIVEN textured geometry at the spawn
WHEN the camera faces surfaces with different light exposure
THEN faces toward the directional light are visibly brighter than perpendicular or opposite faces.

## Scenario 6: Graybox fallback stays readable

GIVEN the baked level is unavailable
WHEN the ROM boots
THEN fallback cubes render mid-grey rather than black.

## Scenario 7: Frame time remains acceptable

GIVEN the camera sees the densest useful view of the room
WHEN the HUD or serial profiler is read
THEN frame time is `<= 33 ms`; otherwise record the miss and file material-batch rendering as follow-up work.

## Cross-cutting checks

- After Inc 1, generated `.lvl` and `.manifest` files remain uncommitted.
- After Inc 2, first-room generated textures are `32x32 RGBA16`.
- After Inc 3, `tests/material_budget_smoke.py` fails on any manifest texture that cannot enter the 3D texture path.
- After Inc 4, `gameplay_smoke`, `movement_parity_smoke`, `climb_parity_smoke`, and `rom_telemetry_test` still pass.
- After Inc 4, `LevelRenderer` no longer owns or frees a monolithic `dpl_` block.
