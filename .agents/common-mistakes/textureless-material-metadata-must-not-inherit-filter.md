# Textureless material metadata must not inherit texture sampling state

## Symptom

Map boots with no visible rooms; logs show malformed MATMETA, room `bad magic`,
and zero resident batches even though the mappack and global collision mesh load.

## Cause

Generated sentinel material `__TB_empty` has no ROM texture path but inherits a
bilinear filter or wrap mode from its source material. The material catalog
rejects the whole sidecar because textureless rows require `filter=none` and no
wrap state. Falling back to the legacy room loader then reads compressed LVL
assets with raw `fopen`, so it reports bad magic rather than loading rooms.

## Fix

Generate textureless sentinel rows with `filter=none`, no wraps, and empty ROM
path. Keep the asset-aware textured-room path enabled without a catalog so its
flat-color fallback still loads compressed LVL assets through `asset_load`.

## Guardrail

Assert the sentinel metadata contract in generator tests and material-budget
tests. Fresh Ares boot should report a loaded material catalog, textured room
loads, nonzero near batches/residents, and no MATMETA or LVL magic errors.
