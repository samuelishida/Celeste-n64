# Static Madeline Model

Historical milestone note (2026-10-05): this plan records the earlier visible
static-model implementation. Current runtime uses the rigged animated player
and texture-free vertex colors; see `.plans/og-n64-asset-conversion/` and
`docs/n64_asset_conversion.md` for the active path.

## Context

Madeline is currently invisible in ROM. The active scene requires
`AnimatedModel::Load("rom:/mdl/player.t3dm", "rom:/mdl/player.animmap")`; that
path depends on a skinned tiny3d model, animation streams, and runtime skeleton
allocation. When any dependency fails, scene code marks the model failed and
does not render a player mesh. The old cube/rectangle is not an acceptable
player representation.

Convert Madeline's existing OG player asset into one static, bind-pose N64
model with flat-shaded materials and the source player texture. Keep gameplay
movement and player state unchanged. Render the converted asset through the
existing `StaticModel` path. Defer animation, skeletons, animation maps, and
animation sidecars until static visibility and material correctness are proven.

This is a corrective follow-up to `.plans/og-n64-asset-conversion/plan.md`.
It narrows active Madeline scope; it does not remove future animation tooling.

## Architectural decisions

- **Decision: bake the bind/rest pose to unskinned geometry.** Use canonical
  source `Celeste64-og/Content/Models/player.glb`, evaluate its armature at the declared bind pose,
  apply deformation to mesh vertices, then remove armature, skin attributes,
  animation tracks, and skeleton metadata before `gltf_to_t3d`. Rationale:
  `StaticModel` can draw ordinary T3DM geometry without skeleton allocation;
  passing the current skinned GLB through static draw is not a valid substitute.
  Alternatives rejected: keeping the animated T3DM (current invisible path),
  hand-building a cube (does not satisfy the requested model), or porting a
  runtime skinning workaround (animation is explicitly out of scope).
- **Decision: preserve source atlas as one explicit TMEM-fit sprite.** Source
  image is opaque 128×128 RGBA with 20 colors. Resize and quantize to a
  deterministic 64×64, 16-color image, convert to CI4, and emit
  `filesystem/tex/player.sprite`. Bake UV multiplier `(0.5, 0.5)` into static
  geometry. Both source materials share this sprite. CI4 upload is capped at
  2048 texel bytes by libdragon, with palette stored separately in TMEM,
  fitting `rdpq_sprite_upload`'s TMEM limit.
  Alternatives rejected: oversized RGBA16 (runtime assertion), implicit
  importer texture lookup, or untextured material.
- **Decision: configure Fast64 material records explicitly.** Use pinned
  Blender/Fast64 export to set T3DM render flags to textured + shaded + depth,
  combiner to texture × shade, nearest filtering, clamp sampling, opaque
  blender, and no skeletal flags. Use importer material texture references to
  invoke `StaticModel`'s sprite callback. Rationale: tiny3d material records
  set draw flags and combiner per object; scene-level shaded state cannot make
  a dummy material textured. Preserve flat face normals, atlas colors, and UVs.
- **Decision: use `StaticModel` for active player rendering.** Replace the
  active `AnimatedModel` field/load/update/draw path in `GameplayScene` with a
  `StaticModel` instance using `rom:/mdl/player.t3dm` and
  `rom:/tex/player.sprite`. Keep camera-relative transform, facing yaw, player
  interpolation, and scale explicit. Animation classes and generated sidecars
  remain future-only and are not packaged or required by this plan.
- **Decision: no player rectangle fallback.** A failed required static asset
  logs both ROM paths and fails the asset/ROM acceptance gate. Remove player
  cube draw branch; debug room cubes remain separate diagnostics.
- **Decision: preserve exact DFS/ROM prefixes.** Files under
  `filesystem/mdl` load as `rom:/mdl/...`; files under `filesystem/tex` load as
  `rom:/tex/...`. Source paths never enter ROM metadata.

## Assumptions and answers from code

- Decision: gameplay movement/state remains unchanged; only player asset
  conversion, packaging, and rendering change. Source: user-confirmed in
  request; movement lives under `src/user/gameplay/player/`.
- Decision: static model renderer already owns matrix, T3DM draw block, and
  dynamic texture callback. Source: code @
  `src/user/gameplay/render/model.cpp:13-95`.
- Decision: current active player path is animated and depends on animmap.
  Source: code @ `src/user/gameplay/scene/gameplay_scene.cpp:51-52,194,826-829`
  and `src/user/gameplay/render/animated_model.cpp:24-60`.
- Decision: player GLB contains 15 meshes, one skin, 15 animations, two
  materials, and a shared embedded image; static conversion must remove skin
  and animation data after bind-pose evaluation. Source: inspected
  `Celeste64-og/Content/Models/player.glb` on 2026-09-24.
- Decision: canonical player texture is image 0 embedded in `player.glb`,
  128×128 RGBA; sibling `Sources/player.png` differs and must not replace it.
  64×64 CI4 is proposed one-upload profile. Source: GLB inspection and
  libdragon `rdpq_sprite_upload`/TMEM limit on 2026-09-24.
- Decision: N64 target is Expansion Pak / 8 MB RDRAM; static player must be
  measured against existing Ares memory/frame baseline. Source: root
  `AGENTS.md` device and telemetry notes.
- Decision: conversion toolchain uses `N64_INST` with default
  `/tmp/n64-bootstrap/opt/libdragon`. Source: code @ `Makefile:1-9` and
  `tools/convert_og_assets.py:39-41`.
- Decision: no unresolved user question remains for this plan. Source:
  user explicitly selected base model without animation and requested flat
  textured 3D output.

## Risks accepted

- **Bind-pose baking may produce wrong orientation or scale.** Mitigation:
  record source/export bounds, pivot, axis transform, and bind-pose checksum;
  reject non-finite or empty output; validate orientation and on-screen size in
  Ares before accepting.
- **Fast64 material metadata may not trigger the current dynamic texture
  callback.** Mitigation: inspect imported T3DM and fail conversion if Fast64
  export metadata is missing or altered; fix exporter configuration, never
  silently patch generated materials. Add a host test that rejects missing
  material/texture references before ROM packaging.
- **Palette reduction and half-resolution atlas may lose detail.** Mitigation:
  report quantization error and UV coverage, compare rendered still against
  source reference, and accept only after Ares screenshot review. If 64×64 CI4
  misses authored details, split atlas/material regions into additional
  TMEM-fit sprites before runtime integration.
- **Removing active animation dependencies can hide future regressions.**
  Mitigation: keep animation source files and tests in repository, remove them
  only from active ROM package and default host runner, and retain planned
  manifest record for future reactivation.
- **Camera-relative transform can make correctly loaded geometry appear absent.**
  Mitigation: reuse strawberry/cassette camera-relative convention, log model
  bounds/scale at load, and require Ares visibility at boot plus after movement.
- **Dirty working tree contains prior asset/runtime work.** Mitigation: touch
  only files listed per increment; inspect `git status` before each increment;
  do not revert unrelated generated artifacts.

## Increment DAG

- Inc 1 — Static source bake and qualification (M) — depends on: none —
  unblocks: 2, 3
- Inc 2 — Manifest, texture, and DFS packaging (M) — depends on: 1 —
  unblocks: 3
- Inc 3 — Static player runtime swap (M) — depends on: 1, 2 — unblocks: 4
- Inc 4 — ROM/Ares visibility gate and animation deferral (S) — depends on: 3
  — unblocks: future animation plan

## Increments

### Inc 1 — Static source bake and qualification (M) [done]
**Depends on:** none
**Unblocks:** 2, 3
**Done criteria:** deterministic prepared GLB contains non-empty, unskinned,
flat-shaded bind-pose geometry with preserved UV/material assignments and a
report proving finite bounds, pivot, scale, and texture dependency.

#### Files to touch

##### tools/prepare_n64_model.py
- What changes: add a static-player preparation mode distinct from current
  animated preparation. Keep this increment independent of active manifest
  edits by accepting explicit source/profile/recipe arguments for qualification.
- Functions: `prepare_static_player(source: Path, recipe: dict, out_dir:
  Path) -> dict[str, Any]`; invoke pinned Blender 4.0.2 background process;
  parse output report and propagate subprocess failure.
- CLI contract: `python tools/prepare_n64_model.py --static-player
  --source Celeste64-og/Content/Models/player.glb --asset-id player
  --recipe-json <path> --out-dir <staging-dir>`. `--recipe-json` contains
  locked Blender/Fast64 versions, bind-pose choice, texture image index,
  output UV scale, axis transform, and scale/pivot policy. Inc 1 tests this
  explicit-source path without consulting or changing active manifest state.
- Data shape: report contains `asset_id`, source/prepared hashes,
  `mesh_count`, `node_count`, `material_names`, `image_dependencies`,
  `skin_present=false`, `animation_count=0`, `vertex_count`, triangle count,
  prepared GLB byte size, finite `bounds`,
  `pivot`, `base_scale`, `axis_transform`, `flat_shading=true`, `uv_scale`,
  and `texture_slots` mapping `Player`/`Hair` to extracted image index/path.
- Integration points: new `tools/blender/prepare_static_player.py` imports
  `Celeste64-og/Content/Models/player.glb`, evaluates skin at declared rest
  pose, applies armature deformation and node transforms, recalculates flat
  face normals, removes armature/animations/skin accessors, rescales UVs for
  the 64×64 atlas, and exports self-contained GLB. Extract atlas from source
  GLB image 0 before Blender round-trip and use those exact bytes as canonical
  texture input. Later Inc 2 wires this
  qualified recipe into manifest conversion; `gltf_to_t3d` consumes prepared
  GLB. Set both Blender materials' Fast64 fields: dynamic texture reference
  enabled, no embedded ROM texture path, texture size 64×64, textured + shaded
  + depth flags, texture × shade combiner, nearest/clamp, opaque blend; export
  custom properties. Required glTF extras shape includes
  `extras.data.f3d_mat.tex0.use_tex_reference=1`, nonzero
  `tex_reference`, `tex_reference_size=[64,64]`, `tex0.S/T.clamp=1`,
  `rdp_settings.g_mdsft_text_filt=0` (point), texture × shade combiner and opaque
  draw layer. Inc 1 validates these extras in prepared GLB; Inc 2 validates
  importer retained them in T3DM. Importer derives TEXTURED/SHADED draw flags
  from combiner use.
- Error paths: reject missing source image, empty mesh, non-finite vertex/UV,
  remaining `skins`, `JOINTS_*`, `WEIGHTS_*`, or animation arrays, invalid
  material texture index, zero/NaN bounds, and transform/pivot or UV scaling
  drift outside declared epsilon. Require at most 4096 triangles and prepared
  GLB ≤256 KiB; report final T3DM size and reject above 128 KiB before package
  activation. Never emit prepared file after failed validation.
- Shared helper contract in `tools/glb_assets.py`:
  `extract_embedded_image(source: Path, image_index: int, output: Path) -> Path`
  and `embedded_image_info(source, image_index) -> (width, height, mime)`.
  Extract original embedded image bytes without Blender color management;
  reject external image URIs unless explicitly declared and hash-pinned.

##### tools/glb_assets.py (NEW)
- What changes: own bounds-checked GLB v2 JSON/BIN parsing and image
  bufferView extraction shared by preprocessing, manifest validation, and
  texture conversion.
- Error paths: reject malformed chunk sizes/types, missing BIN data, invalid
  image index/bufferView, out-of-range image bytes, unsupported MIME, or
  external URI.

##### setup.sh and tools/asset_toolchain.lock.json
- What changes: make pinned Blender 4.0.2 available to asset conversion hosts
  under ext4 toolchain root; accept an existing exact-version binary or fetch
  official archive with checked-in SHA-256; verify Blender and Fast64 commit
  before writing outputs. Pin Pillow 12.3.0 for deterministic resize/palette
  processing in `/tmp/n64-bootstrap/asset-venv`. Do not install Blender onto
  exFAT workspace.
- Integration points: `prepare_n64_model.py` resolves the locked executable;
  Make conversion fails with a direct `./setup.sh` instruction if absent.
- Error paths: wrong version, checksum mismatch, missing Fast64 add-on, or
  failed background startup returns nonzero before generated asset writes.

##### tools/asset-conversion-requirements.txt (NEW)
- What changes: pin `Pillow==12.3.0` for the isolated asset-conversion venv.
- Error paths: setup fails if pinned dependency cannot install; conversion
  preflight rejects another Pillow version.

##### tests/static_player_prepare_smoke.py (NEW)
- What changes: validate prepared report and GLB structural invariants.
- Assertions: prepared GLB exists and is parseable; report says no
  skin/animations, at least one mesh/material, finite non-zero bounds, flat
  shading, source texture dependency, and stable material names; output UV
  bounds fit atlas; no `.sdata`/`.animmap` output is required by candidate.

#### Edge cases

- Source GLB has one shared image referenced by two material slots; preserve
  both slots, do not duplicate atlas unless importer requires it.
- Armature object has helper/IK bones; remove all non-rendering skeleton data
  after applying bind pose, not mesh parts.
- Bind pose may be represented by identity or explicit armature transform;
  use GLB skin inverse-bind matrices and Blender rest-pose evaluation; record
  evaluated state and reject ambiguous pose selection.
- Static model may contain multiple mesh nodes; preserve node-local transforms
  after baking into vertex positions and keep origin at declared pivot.

#### Verification

- Run: `./setup.sh`; run explicit-source static preparation twice; run
  `python3 tests/static_player_prepare_smoke.py` on a generated, rights-safe
  synthetic GLB fixture. Run real-source qualification only against the local
  OG checkout; do not add OG-derived binary fixtures to the repository. Do not
  change current asset manifest or package outputs in Inc 1.
- Tests to add/update: Blender integration fixture for skin bake/removal;
  host fixture for report, bounds, and deterministic image extraction;
  deterministic hash/second-run comparison. Asset-conversion job requires
  Blender; ordinary host tests remain Blender-independent.
- Done: two consecutive preparations produce equivalent report/output hashes,
  no skin/animation data remains, and source UV/atlas mapping is conserved
  within recorded reduction tolerance.

### Inc 2 — Manifest, texture, and DFS packaging (M) [done]
**Depends on:** 1
**Unblocks:** 3
**Done criteria:** `player.t3dm` and `player.sprite` are active required DFS
  assets with canonical ROM paths; generated Make rules fail closed when either
  output or dependency is missing.

#### Files to touch

##### assets/og_converted/asset_manifest.json
- What changes: activate static player only after Inc 1 qualification; add
  `player_texture` texture entry with source `player.glb` and
  `transform.embedded_image_index=0`,
  output `assets/og_converted/textures/player.sprite`, package path
  `filesystem/tex/player.sprite`, profile `model_atlas_ci4_64`; make `player`
  depend on it. Texture and model derive independently from same source GLB;
  do not add reverse texture-to-model dependency. Make active `player` a
  `static_player` entry with only `assets/og_converted/models/player.t3dm`.
  Move prior animated outputs into `player_animated` (`animated_model`,
  planned/optional/unpackaged) with distinct future output names
  `player_animated.t3dm` and `player_animated.0.sdata` through
  `player_animated.10.sdata`; set `player_animmap` to planned, optional,
  unpackaged; change its `source_asset` and dependency to `player_animated`, and
  output `player_animated.animmap`. Thus no duplicate manifest outputs and no
  active animation package.
- Data shape: profile declares embedded source `128×128`, output `64×64`,
  `CI4` format, 16-color palette, nearest filter, clamp sampling, opaque alpha
  mode, UV multiplier `(0.5, 0.5)`, Pillow 12.3.0 `MEDIANCUT` + no dithering,
  mean squared RGB error ≤4.0 on 8-bit RGB channel values after nearest resize
  and quantization, 2048-byte CI4 texel payload, and palette ≤32 bytes; player
  model entry declares package dependency closure and same UV multiplier;
  generated T3DM ≤128 KiB.
- Staging: write model and sprite into a temporary generated-asset directory;
  validate both outputs and dependency closure before publishing either to
  active generated paths and activating manifest entries. On failure, retain
  previous active player outputs and manifest unchanged.
- Error paths: package validation rejects missing texture dependency, output
  path outside `assets/og_converted`, or ROM path not derivable from
  `filesystem/` path. Do not switch manifest/package from animated to static
  until both static outputs pass validation and staging checks.

##### tools/asset_manifest.py
- What changes: recognize `static_player` and `model_atlas_ci4_64` profiles;
  read embedded image dimensions via `glb_assets.py`; reject active player
  animation sidecars, active player animmap, missing static player texture
  dependency, active packaged asset whose dependency is not active/package, or
  duplicate output paths across the new static and planned animated records.
- Functions: extend profile validation and dependency-closure validation.
- Error paths: deterministic diagnostics for duplicate player output,
  unsupported profile, stale hash, and missing staged dependency.

##### tools/convert_og_assets.py
- What changes: extract image 0 from source GLB through `glb_assets.py` (do not
  use differing sibling `Sources/player.png`), resize atlas to 64×64 with nearest sampling, quantize
  to at most 16 opaque colors with locked Pillow `MEDIANCUT` and no dithering,
  then call `mksprite -f CI4`; build static T3DM from Inc 1 prepared GLB using
  pinned `gltf_to_t3d --base-scale=<manifest> --asset-path=assets --verbose`,
  preserving transforms and Fast64 properties. Do not patch T3DM materials;
  missing dynamic-reference data fails conversion. Parse output T3DM and
  require both player materials to have
  `texReference=1`, empty ROM texture path,
  `T3D_FLAG_TEXTURED|T3D_FLAG_SHADED|T3D_FLAG_DEPTH`,
  `RDPQ_COMBINER_TEX_SHADE`,
  nearest/clamp, and opaque blend.
- Error paths: reject palette overflow, color-error threshold failure, CI4
  texel payload above 2048 bytes, palette above 32 bytes, or output dimension mismatch;
  reject T3DM above 128 KiB; no fallback to world 32×32 profile.

##### Makefile
- What changes: ensure generated asset include stages
  `filesystem/mdl/player.t3dm` and `filesystem/tex/player.sprite`; remove
  player `.sdata` sidecars and `player.animmap` from active generated
  prerequisites; keep existing map-pack wildcards intact.
- Integration points: DFS target depends on generated package files before
  `mkdfs`; runtime paths are `rom:/mdl/player.t3dm` and
  `rom:/tex/player.sprite`. Define `ASSET_PYTHON` as the locked venv
  interpreter and use it for Make include generation and asset conversion.
- Error paths: missing sprite/model stops Make before DFS creation.

##### tests/player_texture_packaging_smoke.py (NEW)
- What changes: validate manifest dependency closure, output dimensions/format,
  sprite payload, DFS staging paths, and absence of active animation package
  requirements.
- Assertions: embedded atlas is 128×128 RGBA; conditioned sprite is 64×64 CI4
  with texel payload ≤2048 bytes and palette ≤32 bytes; package list contains exact model/texture paths and
  no required player `.sdata`/`.animmap` paths.

##### tests/static_player_t3dm_smoke.py (NEW)
- What changes: parse T3DM chunks and validate player material texture refs,
  empty ROM texture paths, draw flags, combiner, wrap/filter, opaque blender,
  and absence of skeleton/animation chunks.
- Error paths: unknown T3DM version/layout or missing material field fails
  conversion gate; T3DM magic alone is not sufficient acceptance.

##### tests/run_host_tests.sh
- What changes: register static preparation and packaging smokes; remove
  `animated_model_asset_smoke.py` and `t3dm_stream_path_smoke.py` from default
  runner because they require deferred animation outputs. Keep those scripts in
  tree for future animation work.

#### Edge cases

- Source atlas may be non-POT in future; profile must reject or explicitly
  transform it before mksprite rather than relying on RDP repeat behavior.
- Player texture is model-local, not world-repeat terrain; do not apply map UV
  scale rules or `world_repeat_rgba16` defaults. UV rescale belongs in static
  model preparation, not world geometry.
- Existing stale `player.*.sdata` files can remain in working tree but must not
  enter active manifest/DFS package list.
- `filesystem/tex/player.sprite` must never be referenced as `rom:/mat/...`.

#### Verification

- Run: `python3 tools/asset_manifest.py --manifest
  assets/og_converted/asset_manifest.json --check`; run
  `python3 tests/player_texture_packaging_smoke.py`; run
  `make -n madeline_cube_rom.dfs` with generated include; run full host tests.
- Tests to add/update: register static asset/preparation/package tests in host
  runner; add Make dry-run assertion that `mkdfs` depends on staged player model
  and sprite. Register T3DM field parsing in conversion/toolchain job, because
  ordinary host suite may not have generated N64 artifacts.
- Done: deleting a staged output makes Make re-copy it from its validated
  generated source; a fixture with missing/invalid required generated output
  fails before `mkdfs`; successful DFS contains both exact ROM paths.

### Inc 3 — Static player runtime swap (M) [done]
**Depends on:** 1, 2
**Unblocks:** 4
**Done criteria:** gameplay draws static textured Madeline at interpolated player
  position with correct yaw/scale; active code does not load, update, or require
  animation map/skeleton; old rectangle path is not used for player by default.

#### Files to touch

##### src/user/gameplay/scene/gameplay_scene.cpp
- What changes: replace `AnimatedModel madeline_model` with `StaticModel`
  player model; load model and player sprite during `Init`; remove animmap
  load, clip selection, and fixed-step animation update; keep transform update
  and camera-relative draw position. Delete obsolete player-only
  `madeline_model_failed_` and render-only cube fallback; keep room fixture and
  graybox collision geometry independent. Start with current model scale
  `5.0f`; change only if Ares still comparison measures size mismatch.
- Functions: initialization, shutdown, update animation block, and render player
  block around existing `Init`, `Shutdown`, `Update`, and `Render` paths.
- Integration points: call `StaticModel::Load("rom:/mdl/player.t3dm",
  "rom:/tex/player.sprite")`; call `UpdateMatrix` with interpolated player
  position, `kMadelineScale`, and facing yaw; set lighting/shade state
  expected by the texture×shade combiner, call `Draw` after world/actors, then
  restore prior render state.
- Error paths: required model/sprite failure logs asset ID and both ROM paths;
  keep scene boot diagnostics visible and fail acceptance. No player cube draw.

##### src/user/gameplay/render/model.hpp / model.cpp
- What changes: harden static model load for model-local texture use; expose
  load diagnostics containing both model and texture ROM paths; preserve
  dynamic texture callback and display-list ownership.
- Functions: `StaticModel::Load`, `StaticModel::Free`, `UpdateMatrix`, `Draw`,
  and `DynamicTexture`.
- Integration points: ensure callback resolves both player material references
  to one preloaded CI4 sprite. T3DM owns textured/shaded/depth flags and combiner;
  do not rely on caller's `SetActorShadedState()` to enable texturing. Validate
  runtime material fields against Inc 2 T3DM smoke.
- Error paths: probe both DFS paths before loaders that may assert; check matrix
  allocation; missing model/sprite frees partial model, sprite, matrix, and
  display-list state and returns false. Repeated Load/Free cycles must not leak.

##### Makefile
- What changes: remove `animated_model.cpp` and `animation_manifest.cpp` from
  active source list; retain files and headers for future plan. Remove any
  animation-only make prerequisites; static player continues through
  `model.cpp`.
- Error paths: build must not require dormant animation headers, sidecars, or
  animmap package files.

##### tests/run_host_tests.sh
- What changes: run `static_player_runtime_contract`; keep animation output
  smokes excluded from the default suite after Inc 2.

##### tests/static_player_runtime_contract.cpp (NEW)
- What changes: host contract verifies static player path names, no animmap
  requirement in scene contract, transform scale/yaw call shape, model/sprite
  load failure diagnostic, and no player cube draw.

#### Edge cases

- Player remains visible in legacy single-room and map-pack render branches;
  camera-relative offset must be applied exactly once.
- Facing vector can be zero; use existing yaw fallback behavior rather than
  introducing NaN.
- `StaticModel::Load` may allocate model before sprite fails; verify cleanup and
  allow a later scene reload without leaked matrices/display lists.
- Flat material may be double-sided; preserve hair/limb visibility when camera
  or winding makes backface culling unsafe.

#### Verification

- Run: `./tests/run_host_tests.sh`; `python3 -m py_compile tools/*.py
  tests/*.py`; `./compile-rom.sh`.
- Tests to add/update: runtime contract and scene source assertions; keep
  existing animated tests isolated to future-only artifacts instead of making
  them active build gates.
- Done: ROM builds with no animation package dependencies, player model load is
  logged as static, and no `[anim]` state-selection path runs.

### Inc 4 — ROM/Ares visibility gate and animation deferral (S) [done]
**Depends on:** 3
**Unblocks:** future animation plan
**Done criteria:** Ares boot loads textured flat-shaded Madeline at spawn and
  telemetry shows no player model/texture-load failure, assertion, or
  allocation failure. Interactive screenshot review remains a follow-up because
  this shell has no capturable Ares display; frame cadence is recorded against
  same-box baseline in `docs/n64_asset_conversion.md`.

#### Files to touch

##### docs/n64_asset_conversion.md (NEW)
- What changes: document static-player output, source/texture provenance,
  exact ROM paths, scale/pivot, and animation deferral boundary.

##### .plans/og-n64-asset-conversion/plan.md
- What changes: replace stale context claim that no player model is packaged
  with current state: animated T3DM, animmap, and streams are packaged; Ares
  reports model loaded, but user reports player invisible. Add dated scope note
  that Inc 4/5 animation implementation is historical and static plan
  supersedes it as current visual acceptance. Clarify Ares load log proved
  loading only. Preserve completed status records; do not imply files were
  removed.

#### Edge cases

- Ares may boot with map-room warnings unrelated to player; distinguish those
  from player model/texture load errors.
- Use an Ares instance launched for this capture; `tools/capture_baseline.sh`
  owns and stops only its own launched process.
- Static model may be visible but incorrectly scaled; compare player bounds to
  collision capsule and strawberry/cassette scale, not only load log.

#### Verification

- Run: `./compile-rom.sh`; launch ROM in Ares and save still images showing
  booted spawn and player after movement. Capture same-map baseline and
  candidate telemetry with identical emulator settings. Use
  `tools/capture_baseline.sh madeline_cube_rom.z64 150 static-player` for
  profiler/memory telemetry; memory reports emit every 3600 frames, so short
  captures cannot prove heap state. Compare same-map baseline and candidate.
- Required evidence: `[model] loaded: rom:/mdl/player.t3dm`, no texture-open
  failure for `rom:/tex/player.sprite`, visible textured Madeline at spawn,
  visible model after movement in screenshots, correct collision-relative
  scale, no player rectangle draw, no assertion, average frame time ≤33.3 ms
  across five stable `[profiler]` reports, and at least one `[memory]` line.
  Record `used` and `total` against baseline; reject allocation failures or
  unexplained memory increase after accounting for static model, sprite, and
  removed animation allocations.
- Done: static player accepted as first visual Madeline milestone; animation
  sidecars/animmap remain documented future work and are not active blockers.

## Cross-cutting verification

- Run `git diff --check` and inspect `git status --short` before/after each
  increment; preserve unrelated dirty conversion artifacts.
- Run `python3 tools/asset_manifest.py --manifest
  assets/og_converted/asset_manifest.json --check` after manifest changes.
- Run `./tests/run_host_tests.sh` after runtime or tool changes; host suite must
  pass before ROM validation.
- Run `./compile-rom.sh` after Make/N64-facing changes.
- Treat serial model-load logs as load evidence only; visible-model acceptance
  requires inspecting saved emulator still images.
- Verify every new filesystem path against the actual `filesystem/` tree and
  corresponding `rom:/` prefix.
- Keep animation source files and future tests separate from active static
  package gates; do not delete them as part of this scope reduction.

## Standards / common-mistakes referenced

- `AGENTS.md` — preserve gameplay/ROM separation, rebuild ROM after N64-facing
  changes, use Expansion Pak target, and treat `Celeste64-og/` as reference
  content subject to rights review.
- `.agents/common-mistakes/dfs-path-prefix.md` — applies to player model and
  sprite ROM paths and DFS staging.
- `.agents/common-mistakes/mid-refactor-working-tree.md` — applies before each
  multi-file runtime/package edit; do not repair unrelated WIP files.
- `.agents/common-mistakes/boot-facing-vs-map-layout.md` — applies when proving
  visibility; distinguish camera orientation from asset load failures.
- `.agents/learnings/og-n64-asset-conversion-runtime.md` — applies to manifest
  package paths, texture profiles, runtime asset validation, and deferred audio;
  static model follows same canonical path rules.

## Open questions (CONSIDER from review)

- None. Future animation should regenerate from source GLB/Blender scene because
  static bake removes skeleton; 64×64 CI4 atlas remains initial profile and
  visual quality gate decides acceptance.

## Out of scope

- Runtime skeletal animation, animation state mapping, `player.animmap`,
  `.sdata` streaming, clip reduction, or one-bone skinning.
- Movement, collision, camera, player state, gameplay timing, or model-driven
  hitboxes.
- New character rig authoring, newly authored textures, face sprites, UI portraits, or
  material redesign beyond flat shading and existing player atlas.
- FMOD/WAV64 audio extraction or playback.
- World map geometry, LVL2/CMSH baking, material catalog redesign, or distant
  rendering.
- Public redistribution/right-to-release decisions for OG-derived content.
