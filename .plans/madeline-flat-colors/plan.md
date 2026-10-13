# Madeline Flat Colors

## Context

Replace the active Madeline model's runtime atlas texture with solid vertex
colors baked during asset preparation. Preserve the current bind-pose mesh,
silhouette, transform, and gameplay clearance. Use the source atlas as an
offline color reference so the ROM no longer packages or uploads a player
sprite.

## Assumptions and decisions

- Decision: bake the source base-color image into flat per-face corner colors,
  then export them as glTF `COLOR_0`. Source: user-confirmed request; source
  atlas and Blender bind-pose pipeline in `tools/blender/prepare_static_player.py`.
- Decision: use the existing `SHADE` material combiner without a texture and
  leave the generic `StaticModel` optional texture API intact. Source: code @
  `src/user/gameplay/render/model.cpp:13-70` and Tiny3D Fast64 material pipeline.
- Decision: remove the player sprite from active manifest/package dependencies,
  but retain the unreferenced texture profile tooling for other/future assets.
  Source: code @ `assets/og_converted/asset_manifest.json` and
  `tools/asset_manifest.py`.
- Assumption: keep the current geometry triangle budget (4096), enforce the
  converted T3DM size/vertex budgets, and reject output whose colors/material
  metadata or bounds are invalid. Source: code @ `tools/prepare_n64_model.py`.
- Decision: change only player conversion/package/runtime files; preserve all
  pre-existing unrelated working-tree changes. Source: user workspace state.

## Files to touch

### `tools/blender/prepare_static_player.py`
- What changes: sample the source image at each polygon's UV centroid and write
  the sampled flat color to every loop in that polygon's `COLOR_0` attribute;
  bake material base-color multipliers, force alpha opaque, detach all image
  nodes, and export no image or UV dependency. Report the source image index
  and SHA-256 so the bake remains reproducible across machines.
- Function(s): add UV/image sampling helpers; change `_set_fast64_properties`
  to texture-free `SHADE`; change `_report` to report vertex-color mode and no
  image/texture slots.
- Data shapes: report includes `color_source`, `vertex_color_attribute`, and
  `image_dependencies=[]`; GLB has `COLOR_0`, no `images`, `textures`,
  `TEXCOORD_0`, or texture-reference metadata.
- Integration points: reuse source material's base-color image while baking;
  keep bind-pose geometry, normals, scale, axis, and pivot unchanged.
- Error paths: reject missing UV/image material references, invalid UV/image
  bounds, no output color attribute, non-finite channels, or empty geometry.

### `tools/prepare_n64_model.py`
- What changes: validate the flat-color player profile and reject remaining
  texture/image/UV dependencies or Fast64 textured material fields; record
  color attribute presence/count and source-image hash in the prepared report.
- Function(s): `_validate_static_glb`, `_host_static_fixture`, and static
  preparation report handling.
- Data shapes: each primitive requires `COLOR_0` with the same count as
  `POSITION`; player materials require `RDPQ_COMBINER_SHADE` and no texture
  reference.
- Integration points: `prepare_static_player` continues deterministic report
  and output hashing; prepared-player output remains the input to
  `gltf_to_t3d`.
- Error paths: fail conversion on absent/mismatched colors, texture remnants,
  invalid material combiner, or existing bounds/triangle/byte budget failures.

### `tools/player_color_bake.py`
- What changes: add a host-safe nearest-pixel UV sampler used by the Blender
  bake and its deterministic known-color fixture.
- Function(s): `sample_nearest_rgba(pixels, width, height, uv, factor)`.
- Data shapes: flat RGBA pixel sequence and an opaque RGBA result.
- Error paths: reject invalid image dimensions/buffer, non-finite UVs, and
  invalid material factors.

### `assets/og_converted/asset_manifest.json`
- What changes: remove `player_texture` from the active player dependency and
  mark the atlas asset non-required and non-packaged.
- Integration points: no `filesystem/tex/player.sprite` dependency in DFS;
  keep the GLB source image available to the offline color bake.
- Error paths: manifest validation must continue to accept the player without
  a generated sprite.

### `src/user/gameplay/scene/gameplay_scene.cpp`
- What changes: load the static Madeline model without `kMadelineTexturePath`.
- Function(s): player asset constants and `GameplayScene::Init`.
- Integration points: generic `StaticModel::Draw` uses the T3DM's vertex shade
  material; preserve existing position, scale, yaw, and clearance.
- Error paths: existing model-load diagnostic names only the required model.

### `tests/player_color_bake_smoke.py`, `tests/static_player_prepare_smoke.py`, `tests/static_player_t3dm_smoke.py`, `tests/player_texture_packaging_smoke.py`, `tests/static_player_runtime_contract.cpp`, `tests/run_host_tests.sh`
- What changes: assert deterministic `COLOR_0` output, no texture/image/UV
  references, `SHADE` material records, no player sprite in the manifest/DFS,
  texture-free runtime load, a known per-triangle color through GLB and T3DM,
  and converted vertex/byte budgets.
- Verification: run focused static-player smokes and the ROM build.

### `docs/n64_asset_conversion.md`
- What changes: document the texture-free flat-color player conversion and
  the source atlas's offline-only role.

## Edge cases

- UVs on image boundaries: clamp to the nearest valid texel center.
- Materials with separate material/image objects sharing an image: bake the
  material's image and base-color factor for each mesh independently.
- Alpha is forced to opaque because the player material is opaque in the
  runtime draw path.
- Degenerate/missing UV triangles or material image: fail with mesh/material
  context rather than emit default black geometry.
- Output geometry changes bounds or collision fit: preserve geometry positions
  and rerun the existing collision-fit check against prepared output.
- Exporter duplicates vertices at material/color boundaries: enforce both
  converted vertex count and T3DM byte budgets before packaging.
- Fast64/T3DM importer ignores vertex colors or expects the wrong combiner:
  fail the generated T3DM smoke before packaging.

## Open questions (CONSIDER from review)

- None. Known UV samples and material factors are covered by
  `player_color_bake_smoke.py`.

## Verification

- Run: `python tests/static_player_prepare_smoke.py`,
  `python tests/player_texture_packaging_smoke.py`,
  `python tests/static_player_t3dm_smoke.py`,
  `./tests/run_host_tests.sh`, and `./compile-rom.sh`.
- Manual: inspect the generated T3DM material and prepared GLB; confirm the
  package contains `mdl/player.t3dm` and no `tex/player.sprite`.
- Done criteria: the ROM builds with the vertex-colored player, no player
  texture is loaded or packaged, collision-fit checks pass, and source geometry
  remains within existing budgets.

## Standards / common-mistakes referenced

- None found in `.agents/standards/` or `.agents/common-mistakes/` index files.

## Estimated scope

M
