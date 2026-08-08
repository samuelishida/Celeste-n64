# DFS path prefix must match filesystem/ directory layout

## Symptom
`sprite_load("rom:/mat/metal_floor_1.sprite")` — or any `rom:/<dir>/` path —
assertion-fails at runtime with `error opening file rom:/<dir>/<file>` despite the
file existing under `filesystem/`.

## Cause
`mkdfs` packs the `filesystem/` tree verbatim. A file at
`filesystem/tex/metal_floor_1.sprite` is accessible at `rom:/tex/metal_floor_1.sprite`,
**not** `rom:/mat/metal_floor_1.sprite`. The `mat` prefix in the hardcoded path
does not match any directory in `filesystem/`.

## Fix
Align the load path with the actual `filesystem/` subdirectory that contains the file.
The material catalog (`material_catalog.cpp`) correctly uses `rom:/tex/%s.sprite`.
New code should use the same prefix if the sprite lives in `filesystem/tex/`.

## Prevention
- Always verify the `filesystem/` subtree before writing a `rom:/` load path.
- If adding a sprite that lives alongside level textures, use `rom:/tex/`.
- If a new subdirectory is needed, add a `filesystem/<name>/` rule in the
  Makefile and add it to the DFS packing prerequisites.
