# OG-to-N64 Asset Conversion

Scope note (2026-09-24): historical animation increments proved model-load
telemetry but did not prove visible Madeline. `.plans/static-madeline-model/`
supersedes them for current visual acceptance: static bind-pose geometry and
`rom:/tex/player.sprite` are active; animation files stay future-only.

Current implementation note (2026-10-05): the animated player path is active
again. The runtime keeps the qualified tiny3d skeleton/clips but bakes flat
vertex colors offline, including a red-orange tint for the grayscale Hair
material. The static-model section below records the earlier milestone only.

## Context

The project already bakes OG TrenchBroom maps into LVL2/CMSH/map-pack files and converts selected models and textures. The current bulk converter is not reproducible as a ROM asset source: it discards GLB materials and transforms, skips failed conversions while returning success, applies a 32×32 texture rule to only six names, and is not the source of truth for DFS packaging. The static-player follow-up now packages a bind-pose `player.t3dm` plus `player.sprite`; animation artifacts remain planned and inactive. Audio banks remain FMOD containers and have no extraction or runtime event path.

Build a staged, deterministic asset pipeline for libdragon and tiny3d. Keep map geometry on the existing OG world IR → LVL2/CMSH/map-pack path. First current visual milestone is a faithfully textured strawberry and static bind-pose Madeline on the current map; animation is deferred. Register assets and their dependencies in one manifest, condition textures to N64 memory/material constraints while preserving map repeat scale, and make missing required outputs fail conversion/build. Add exported-audio conversion and playback as a separate increment because source-bank extraction is external and project has no WAV64 playback path yet.

## Architectural decisions

- **Decision: preserve canonical map pipeline.** Continue OG `.map` → `tools/ogworld/` → LVL2/CMSH/map-pack. Model conversion does not replace collision or world chunk baking. Rationale: runtime currently owns one global collision mesh and streams LVL2 visual rooms; `tools/writers/t3dm_writer.py` is still unimplemented.
- **Decision: make a source asset manifest authoritative for converted-artifact packaging.** Manifest declares source, output(s), recipe/profile, required status, and dependencies; build derives model/texture/font/audio DFS inputs from validated packaged entries. Keep the specialized map-pack wildcard packaging and level material manifests intact. Rationale: current `DFS_MDL_FILES`, `DFS_TEX_FILES`, and font list are manually curated, while `.manifest` and `.mappack` files describe different runtime data.
- **Decision: use Fast64-authored glTF materials for tiny3d models.** Blender preprocessing normalizes scene transforms/pivots and produces Fast64-compatible glTF with exported custom properties. Tiny3d conversion must retain material references; `--ignore-materials` and `--ignore-transforms` remain diagnostic options only. Rationale: tiny3d importer documentation states Fast64 materials are supported and requires custom-property export.
- **Decision: texture profiles are per asset role.** Repeating map materials begin at 32×32 RGBA16. Preserve U/V repeat scale with independent `output_width/source_width` and `output_height/source_height` UV factors. Decals and cutouts use explicit alpha/filter/wrap profiles; do not treat them as repeating opaque tiles. Model atlases may use a different profile when a 32×32 map tile would destroy character detail.
- **Decision: defer tiny3d skeletal runtime for Madeline.** Current active player is an unskinned bind-pose T3DM rendered through `StaticModel`; skeletons, clips, animmap, and stream sidecars remain planned artifacts until static visibility is accepted. Preserve the future animated conversion path for a later plan.
- **Decision: audio bank extraction remains an explicit input boundary.** Do not attempt to parse FMOD `.bank` files in the asset converter. Accept licensed/exported WAV sources, convert to WAV64, and represent event/channel/loop intent in manifest metadata.
- **Decision: pin conversion tools in a checked-in lock file.** Blender, Fast64, `gltf_to_t3d`, tiny3d, and libdragon versions/commits become part of each artifact recipe; preflight rejects drift. Current checkout has no installed N64 toolchain, so Inc 1 selects and records versions only after a clean bootstrap is available.

## Assumptions and answers from code

- Decision: LVL2/CMSH/map-pack remains world geometry path. Source: user-confirmed; current bake entry point is `tools/bake_interconnected_map.py` and Make target is `bake-forsaken-city`.
- Decision: initial world texture target is 32×32 RGBA16 with repeat-scale correction. Source: user-confirmed; renderer packs UVs as pixel coordinates and wraps the 32-pixel tile in `src/user/gameplay/render/textured_room_renderer.cpp:261-270`.
- Decision: strawberry is static conversion/material validation; it already loads and draws through `StaticModel` in `src/user/gameplay/scene/gameplay_scene.cpp:807-808,1337-1359`.
- Decision: current Madeline path is static model + sprite with explicit packaging; animated path is future-only. Source: static-player follow-up `.plans/static-madeline-model/plan.md`, `GameplayScene`, and manifest records.
- Decision: tiny3d supports skinned meshes/animations, with one bone per vertex and up to three bones per triangle. Source: official tiny3d README and local `tiny3d-main/examples/08_animation/main.c`.
- Decision: OG audio banks need externally exported samples, and no WAV64 playback integration exists. Source: `tools/convert_og_assets.py:377-396`; `src/user/` has no audio playback call path.
- Assumption: asset work targets local development ROM. Public redistribution of OG-derived content requires a separate rights check. Source: project `AGENTS.md` reference-material guidance.
- Decision: supported N64 device target is Expansion Pak / 8 MB RDRAM; new animation allocations must be measured against current Ares baseline. Source: project `AGENTS.md` target and telemetry notes.
- Decision: canonical toolchain root is `N64_INST=/tmp/n64-bootstrap/opt/libdragon`, overridable through environment. Source: `Makefile:4-5`; converter currently defaults to a conflicting `/tmp/n64-toolchain-root` path at `tools/convert_og_assets.py:39-41`.
- Decision: no `.agents/standards/` directory or common-mistakes index exists. Applicable repository guidance is root `AGENTS.md`, `.agents/common-mistakes/dfs-path-prefix.md`, and the indexed learnings listed below.

## Risks accepted

- **Fast64 conversion may not preserve source rig/material semantics in one pass.** Mitigation: qualify strawberry first, then player import in isolation; gate assets on output inspection and Ares rendering rather than converter exit code alone.
- **One-bone skinning may crease joints.** Mitigation: bake dominant influences, inspect representative poses, and simplify/re-author the rig only where visible artifacts exceed acceptance.
- **32×32 conversion may lose decal or character detail.** Mitigation: profiles encode role/alpha and permit smaller atlases or tiled/split output where 32×32 is unsuitable; world repeat materials stay under TMEM limit.
- **Animation streams and skeleton matrices consume Expansion Pak heap and frame time.** Mitigation: package only required clips initially; measure stable `[memory]` and frame telemetry against same-map baseline in Ares.
- **FMOD export cannot be automated from current checkout.** Mitigation: keep extraction manual and document WAV input inventory, event mapping, and loop metadata; no bank parser is in scope.
- **Generated manifest and DFS paths can disagree.** Mitigation: validate every packaged output path against `filesystem/` layout and ROM prefix; retain exact artifact hashes and fail on missing required assets.
- **OG asset redistribution rights are separate from source-code license.** Mitigation: keep release packaging gated on rights review; plan covers local conversion only.

## Increment DAG

- Inc 1 — Manifest and fail-closed asset build (M) — depends on: none — unblocks: 2, 3, 4, 6
- Inc 2 — World texture profiles and UV preservation (M) — depends on: 1 — unblocks: 3, 4
- Inc 3 — Faithful strawberry conversion (M) — depends on: 1, 2 — unblocks: 4
- Inc 4 — Madeline rig conversion qualification (M) — depends on: 1, 2, 3 — unblocks: 5
- Inc 5 — Animated Madeline runtime integration (L) — depends on: 3, 4 — unblocks: first visual milestone
- Inc 6 — Exported-audio conversion and event playback (M) — depends on: 1 — unblocks: audio-enabled ROM

Inc 3 must precede Inc 4 because both extend shared model-preparation and manifest/build files. Inc 6 may proceed in parallel after Inc 1. First visual milestone closes after Inc 5; audio remains independent.

## Increments

### Inc 1 — Manifest and fail-closed asset build (M)
**Depends on:** none
**Unblocks:** 2, 3, 4, 6
**Status:** done (2026-09-24; bootstrap, manifest/staging checks, and N64 Make/ROM build pass)
**Done criteria:** one versioned manifest describes all OG-derived assets currently packaged in the ROM plus strawberry/player targets; tool lock records exact versions/commits; conversion and DFS packaging derive from active entries; missing or invalid required outputs fail the command and ROM build with asset ID and source path.

#### Files to touch

##### assets/og_converted/asset_manifest.json (NEW)
- What changes: canonical OG-derived asset registry, separate from LVL material lists and map-pack manifests.
- Data shape: top-level `schema_version`, `pipeline_version`, `assets[]`; each entry has `id`, `kind`, `source`, `outputs[]`, `status` (`active` or `planned`), `required`, `package`, `profile`, `transform`, `dependencies[]`, optional `animations[]` and `audio_event` metadata. Each output declares converted `path`, `format`, exact `package_path` under `filesystem/`, and generated metadata/hash after conversion. Inc 1 marks current shipped assets, including existing strawberry output, active. Missing player outputs and new planned audio outputs remain unpackaged until producer increments validate them, then flip to active/required/package.
- Integration points: converter reads recipe and writes declared outputs; build generator reads package=true entries. Paths are repository-relative and reject traversal outside source/output roots.
- Error paths: duplicate IDs/outputs, missing sources, unknown profiles, invalid dependency references, hash mismatch, missing required output, or unexpected required conversion result fail nonzero. Optional failures report status but do not masquerade as complete.

##### tools/asset_manifest.py (NEW)
- What changes: parse, validate, inventory, and emit Make-compatible package lists from manifest.
- Functions: `load_manifest(path) -> AssetManifest`; `validate_manifest(manifest, roots) -> list[Diagnostic]`; `write_make_include(manifest, output_path) -> None`.
- Integration points: `tools/convert_og_assets.py` delegates manifest validation. Generated `build/og_assets.mk` declares converted-output targets, staging/copy targets from each manifest `outputs[].path` to exact `outputs[].package_path`, and `OG_ASSET_PACKAGE_FILES`; `madeline_cube_rom.dfs` depends on those staged package files before `mkdfs` packs `filesystem/`. Keep map-pack wildcards intact. The include-generation rule must exist before the generated include is parsed, and its failure must stop Make.
- Error paths: deterministic diagnostics on stderr; nonzero exit for every required asset/build error.

##### tools/asset_toolchain.lock.json (NEW)
- What changes: pin Blender version, Fast64 commit, libdragon preview commit, tiny3d commit, and generated importer version; record the conversion profile schema version.
- Error paths: preflight rejects unavailable or mismatched tools with expected and observed versions before writing outputs.

##### tools/convert_og_assets.py
- What changes: add manifest-driven targeted conversion and strict mode; retain explicit optional batch behavior for out-of-scope legacy categories until registered.
- Functions: revise `convert_model`, `convert_first_room_texture`, `main`; remove silent-success behavior for required assets; resolve `N64_INST` consistently with Makefile and PATH fallback.
- Error paths: propagate required conversion failure to process exit; stale outputs cannot satisfy a failed source conversion unless manifest hash matches current source/recipe.

##### Makefile
- What changes: include `build/og_assets.mk`, generated by `tools/asset_manifest.py`, for registered OG model/texture/font/audio package paths; leave generated map-pack file wildcard intact.
- Integration points: generated include has concrete rules that run strict conversion for required active assets and stage each declared output at its `package_path`; `madeline_cube_rom.dfs` depends on those staged package files as well as existing explicit/map-pack inputs, then invokes existing `mkdfs filesystem/`. Make regenerates the include when manifest or generator changes. A missing staged file must fail before `mkdfs`, not merely appear in a variable or dependency list.
- Error paths: manifest include generation failure stops make before DFS creation. Planned/unpackaged assets produce no DFS prerequisite. Runtime paths must match `filesystem/mdl`, `filesystem/tex`, and `filesystem/audio` layout.

##### tests/asset_manifest_smoke.py (NEW)
- What changes: cover duplicate/missing records, required vs optional failures, dependency closure, path traversal rejection, deterministic package output, and stale source hashes.

#### Edge cases
- An optional conversion failure remains visible in summary but does not enter package list.
- A required model may have multiple outputs (T3DM plus streamed animation sidecars); all outputs must exist and validate.
- Existing `.manifest` files remain level material-ID order, not asset registry inputs.

#### Verification
- Bootstrap prerequisite: if `$(N64_INST)/include/n64.mk` is unavailable, run `./setup.sh` before Make-based verification; Make parses that include even for `-n` dry runs. Pin and record tool versions only after bootstrap succeeds.
- Run: `python3 tests/asset_manifest_smoke.py`; generate `build/og_assets.mk`; then run `make -n madeline_cube_rom.dfs` with bootstrapped `N64_INST` and generated include. Verify a deleted staged file is recreated from its converted output; separately force a required conversion failure and assert Make exits before invoking `mkdfs`.
- Tests to add/update: `tests/asset_manifest_smoke.py` as above; no N64 toolchain needed for schema validation.
- Done: required outputs, hashes, dependencies, and package paths validate deterministically; one missing required output causes nonzero build.

### Inc 2 — World texture profiles and UV preservation (M)
**Depends on:** 1
**Unblocks:** 3, 4
**Status:** done (2026-09-24; profile/UV/bake/runtime metadata checks and host suite pass)
**Done criteria:** every material referenced by the Forsaken City map-pack has explicit role/profile and packaged output; repeating-material scale matches source; output texture residency fits declared N64 TMEM budget; alpha decals preserve transparency behavior.

#### Files to touch

##### assets/og_converted/asset_manifest.json
- What changes: register every source image referenced by `1.map` with profile `world_repeat_rgba16` or `world_decal_rgba16`, input dimensions, output dimensions, alpha policy, filter/wrap policy, and package dependency.
- Data shape: texture profile includes `max_width`, `max_height`, `format`, `alpha_mode`, `wrap_s`, `wrap_t`, `filter`, `resize_filter`, and `uv_scale_mode`.
- Error paths: map material absent from registry or profile cannot fit TMEM fails bake/package validation.

##### tools/convert_og_assets.py
- What changes: replace six-name texture special case with profile-based conditioning for every map material; keep source dimensions and apply nearest/box filter per declared profile; output exact `.sprite` format.
- Functions: `convert_texture(src, dst_dir, profile) -> ConversionResult`; result records source/output dimensions and per-axis UV multipliers.
- Error paths: reject unsupported alpha/format/profile combinations; do not silently downscale decals as opaque repeat textures.

##### tools/ogworld/model.py and tools/ogworld/parse.py
- What changes: add `texture_profiles: Dict[str, TextureProfile]` to `WorldBuild`, keyed by material name; load profile dimensions/role from the validated asset manifest while preserving the existing ordered `texture_manifest` material IDs.
- Data shape: `TextureProfile` carries source/output width/height, `uv_scale_u/v`, render role, alpha mode, wrap mode, filter, and texture path.
- Error paths: every renderable map material must have exactly one profile; sentinel/non-rendered materials receive explicit no-texture profiles.

##### tools/ogmap_lib/texture_mapping.py
- What changes: scale raw Quake texel coordinates by per-material `(out_width/source_width, out_height/source_height)` before serialization; do not alter translation or rotation semantics.
- Functions: `compute_uv(point, face, scale=0.2, uv_scale=(1.0,1.0)) -> (u,v)`.
- Integration points: `tools/ogworld/geometry.py` passes profile-derived UV scale while constructing world polygons; LVL2 continues to store normalized texel coordinates consumed by existing renderer.

##### tools/ogworld/geometry.py
- What changes: resolve each face material profile and pass its scale to `compute_uv` before creating `WorldPolygon`.
- Error paths: unknown texture dimensions fail strict bake instead of silently assigning material ID zero.

##### tools/writers/lvl_world_writer.py, tools/bake_interconnected_map.py, and filesystem/lvl/forsyken-city/<pack>.matmeta
- What changes: emit deterministic material metadata in material-ID order alongside LVL2 outputs. Each record maps material ID/name to texture path and render/alpha/wrap/filter profile so runtime receives the same profile that drove UV scaling.
- Data shape: UTF-8 tab-delimited file (one literal tab between fields), header `MATMETA\t1\t<count>`, then ordered rows with exactly ten fields: `id`, `name`, `rom_path`, `role`, `alpha_mode`, `wrap_s`, `wrap_t`, `filter`, `uv_scale_u`, `uv_scale_v`. Material names reject tabs/newlines; no CSV quoting or escaping is used. `rom_path` must resolve to the declared manifest `package_path` after stripping `filesystem/` and prefixing `rom:/`.
- Integration points: add `.matmeta` to map-pack stage/copy/DFS wildcard; existing plain `.manifest` remains unchanged.
- Error paths: metadata count/order must match material names exactly; missing record fails bake.

##### Makefile
- What changes: package map-pack `*.matmeta` alongside room/material files and add its producer sources to bake prerequisites.
- Error paths: ROM build fails when active materials lack their matching metadata file.

##### src/user/gameplay/render/material_catalog.hpp and material_catalog.cpp
- What changes: `MaterialCatalog::Load(level_name)` loads sibling `.matmeta` beside existing `.manifest`; expose `MaterialProfileFor(material_id)` with explicit texture path, wrap, alpha/blend, and filter flags. Legacy levels without `.matmeta` retain current repeat/opaque defaults.
- Error paths: malformed metadata or count/name/order mismatch fails map render initialization with material ID and name.

##### src/user/gameplay/render/textured_room_renderer.cpp
- What changes: choose per-material texture wrap and blender/combiner state from catalog profile; retain `REPEAT_INFINITE` textured opaque terrain path, use explicit alpha-capable draw mode for decals/cutouts.
- Error paths: unsupported alpha mode is a bake-time error, never silently rendered as opaque/repeating material.

##### tests/material_budget_smoke.py and tests/texture_profile_smoke.py (NEW)
- What changes: validate all names in the Forsaken City pack's material list and `.matmeta`, not only first-room materials; parse generated sprite dimensions/format; assert RGBA16 payload ≤4096 bytes and expected UV repeat ratio for rectangular and 32×32 sources; verify decal alpha/profile survives conditioning and reaches runtime metadata.
- Integration points: register focused script in `tests/run_host_tests.sh` after it can run without N64 tools.

#### Edge cases
- Rectangular source: fit within profile without stretching; compute U/V UV scales independently.
- 32×32 source: UV multiplier remains 1 on each axis.
- Alpha decal: explicit clamp/blend/alpha path, not `REPEAT_INFINITE` opaque terrain behavior.
- `__TB_empty` and `invisible`: non-rendered/sentinel material entries remain defined without loading sprites.
- Material count remains within `MaterialCatalog::kMaxMaterials` or bake fails with count and offending names.

#### Verification
- Run: `python3 tests/texture_profile_smoke.py`; `make bake-forsaken-city`; `python3 tests/material_budget_smoke.py`.
- Tests to add/update: whole-map profile/budget check and UV-scale fixtures.
- Done: source→output UV repeat ratio and alpha handling are verified for every material used in current map.

### Inc 3 — Faithful strawberry conversion (M)
**Depends on:** 1, 2
**Unblocks:** 4
**Status:** done (2026-09-24; prepared GLB, conditioned texture, T3DM, host checks, and Ares model-load smoke pass)
**Done criteria:** existing strawberry model path loads a conditioned T3DM with preserved mesh, color/texture, origin and scale; collectible appearance is verified in Ares.

#### Files to touch

##### tools/prepare_n64_model.py (NEW)
- What changes: Blender background preprocessing for static GLB/Blend inputs: apply object transforms into mesh data, normalize game axes/scale/pivot by manifest recipe, create Fast64-compatible material properties (texture-reference enabled, texture name/size, combiner, wrap/filter, alpha/render mode), export glTF with custom properties enabled, and extract embedded image dependencies.
- Functions: `prepare_static_model(source, recipe, out_dir) -> PreparedModel`; CLI `--asset-id strawberry --manifest ...`.
- Data shape: manifest recipe contains explicit source-to-game 4×4 coordinate transform, unit scale, pivot mode/offset, and Fast64 material settings; report outputs prepared `.gltf/.glb`, extracted texture images, applied matrix, material names, bounds, pivot, and source/tool hashes.
- Error paths: reject unapplied transforms, missing material images, empty geometry, invalid bounds, unsupported alpha, or export that changes mesh/material counts beyond declared tolerance.

##### assets/og_converted/asset_manifest.json
- What changes: strawberry recipe records normalized origin, unit scale conversion, target material profile(s), source GLB/Blend choice, embedded texture dependency, `filesystem/mdl/strawberry.t3dm` output, and required/package status.

##### Makefile
- What changes: run Blender preparation plus `gltf_to_t3d` for registered model; package generated T3DM and dependent sprites via manifest-derived lists.

##### src/user/gameplay/render/model.cpp and src/user/gameplay/render/model.hpp
- What changes: allow `StaticModel` to use material callback / draw configuration needed by converted Fast64 texture references, following existing `T3dmRoomRenderer` callback and `rom:/tex/` convention.
- Integration points: current `GameplayScene` strawberry actor continues to own its existing load/update/draw path; avoid duplicate actor integration.
- Error paths: missing referenced sprite fails model load with material and ROM path in diagnostic; no silent untextured substitute for a required prop.

##### tests/static_model_asset_smoke.py (NEW)
- What changes: validate converted T3DM signature, required material references, source dependency hashes, non-empty bounds, and sprite outputs. Toolchain-backed conversion check runs only in N64 asset job.

#### Edge cases
- Preserve strawberry geometry orientation in `+Y` up world; transformed root node must not shift origin.
- Keep StrawberryActor bob motion; asset conversion does not bake bob into model.
- Material images embedded in GLB are extracted into separate manifest-tracked sprites; no source-tree absolute paths in ROM metadata.

#### Verification
- Run: `python3 tests/static_model_asset_smoke.py`; ROM build; Ares render at collectible and live StrawberryActor positions.
- Tests to add/update: deterministic preprocessing report and required texture-reference validation.
- Done: no missing-material warning; strawberry shape, color, pivot, and world scale match authored reference in Ares.

### Inc 4 — Madeline rig conversion qualification (M)
**Status:** superseded for active player by `.plans/static-madeline-model/plan.md` (2026-09-24); retained as future animation qualification.
**Depends on:** 1, 2, 3
**Unblocks:** 5
**Status:** done (2026-09-24; skin-root normalization, tiny3d conversion, clip/skeleton smoke pass)
**Done criteria:** player Blender source exports valid Fast64 glTF and tiny3d T3DM with required skeleton and named clips; rig and material constraints pass offline validation before runtime integration.

#### Files to touch

##### tools/prepare_n64_model.py
- What changes: add armature-safe path to shared converter after Inc 3; apply explicit source-to-game matrix and unit scale while preserving bind pose, rest pose, bone hierarchy, skin weights, animation tracks, and pivot.
- Functions: `prepare_animated_model(source, recipe, out_dir) -> PreparedAnimatedModel`; validate dominant influence selection for each vertex and bone count per triangle.
- Data shape: prepared model report contains joint names/count, clip names/durations, sample rate, vertex influence histogram, max unique bones/triangle, bind-pose bounds, texture dependencies, and dropped/rewritten channel report.
- Error paths: fail when transform normalization changes bind pose beyond tolerance, any required clip is lost, non-finite animation channel exists, an active vertex has no valid bone, or triangle exceeds three unique bone indices after dominant-weight assignment.

##### assets/og_converted/asset_manifest.json
- What changes: player records only gameplay clips required for initial state mapping (`idle`, `run`, `jump/fall`, `dash`, `climb`) and their exact source clip names/loop policy; register model plus tiny3d animation sidecar outputs as one required group.
- Error paths: fail if source clip aliases do not resolve uniquely or any required output is absent.

##### tests/animated_model_asset_smoke.py (NEW)
- What changes: run structural report checks on skeleton hierarchy, clip inventory, dominant weights, per-triangle bone limit, and bind-pose bounds; compare normalized source bind pose with exported model sample within defined epsilon.

#### Edge cases
- Weight ties use stable lowest-bone-index rule so generated artifacts remain deterministic.
- Preserve non-deforming attachment bones only if an exported selected clip or runtime attachment needs them; otherwise record removal.
- Player GLB clip inventory: `Bad.Idle`, `Climb.Idle`, `Climb.Root`, `Climb.Up`, `Dash`, `Idle`, `Idle.001`, `Jump`, `Run`, `Skid`, `StrawberryGrab`, `Turn.L`, `Turn.R`, `Cube.008Action.002`, `Cube.010Action`. No dedicated fall clip exists; map jump/fall to `Jump` initially. Climb selects `Climb.Idle` or `Climb.Up` by vertical input. Source: `Celeste64-og/Content/Models/player.glb`.
- Record and enforce Blender/Fast64 versions and exporter options from Inc 1 lock file; importer custom-property export must be enabled, and prepared coordinate/pivot matrices must be included in asset report.
- Package only selected clips; all extra clips remain optional.
- Tiny3d stores animation streams alongside model metadata and supports ROM streaming; manifest dependency closure must include all streams emitted by importer.

#### Verification
- Run: Blender background export; pinned `gltf_to_t3d`; `python3 tests/animated_model_asset_smoke.py`.
- Tests to add/update: required clip/sidecar completeness and deterministic conversion metadata.
- Done: generated model imports in tiny3d and offline reports preserve required rig, materials, clips, and skinning limits.

### Inc 5 — Animated Madeline runtime integration (L)
**Depends on:** 3, 4
**Unblocks:** first visual milestone
**Status:** reactivated (2026-10-05); the active animated flat-color player uses this runtime path with the current selected clips and package streams.
**Done criteria:** map gameplay state selects corresponding Madeline clip; model animates and renders in Ares without cube fallback; stable heap and frame telemetry meet captured baseline envelope.

#### Files to touch

##### src/user/gameplay/render/animation_manifest.hpp and animation_manifest.cpp (NEW)
- What changes: define `AnimationManifest` and `AnimationClipRecord`, parse/validate ROM-resident animmap data, and expose clip lookup by exact clip name.
- Data contract: `player.animmap` header is `ANIMMAP\t1\t<count>`; each following row has exactly three tab-delimited fields: `clip_name`, `rom_stream_path`, `loop_policy`. Loop policy is `loop` or `once`; clip names and paths reject tabs/newlines; path must be `rom:/mdl/...`. Do not encode player-state mapping here—gameplay's tested state-to-clip mapping selects names, including `Climb.Idle` versus `Climb.Up`.
- Error paths: reject unsupported version, count mismatch, malformed rows, duplicate clip names/stream paths, unknown loop policy, or non-ROM path.

##### src/user/gameplay/render/animated_model.hpp (NEW)
- What changes: own `T3DModel`, `T3DSkeleton`, named `T3DAnim` instances, playback state, buffered bone matrices, and draw transform.
- Functions: `bool Load(const char* model_path, const char* animmap_path)`; `void SetClip(PlayerAnimState state, bool restart)`; `void Update(float dt)`; `void Draw(const T3DMat4FP* matrix)`; `void Free()`. `Load` parses ROM-resident `rom:/mdl/player.animmap` into an `AnimationManifest` value owned by the renderer; do not make ROM code consume host-side `asset_manifest.json`.
- Integration points: use `t3d_skeleton_create_buffered`, `t3d_anim_create/attach/update`, `t3d_skeleton_update`, `t3d_skeleton_use`, and `t3d_model_draw_skinned`; retain streamed sidecar paths through asset loader.
- Data contract: before `t3d_anim_create`, validate every animation chunk's `filePath` against its animmap clip record and host-manifest `package_path`; package every sidecar at that exact path. Inc 4 generates this file from the host manifest only after model/clip qualification; Inc 5 adds it to the generated package targets.
- Error paths: missing clip/stream, path mismatch, or skeleton allocation failure is fatal for required player model and reports asset ID/clip/path; no silent cube fallback in release gate.

##### src/user/gameplay/scene/gameplay_scene.hpp and src/user/gameplay/scene/gameplay_scene.cpp
- What changes: replace static player model with `AnimatedModel`; derive clip selection from existing player state transitions; update animation using simulation delta time; preserve camera-relative transform, facing yaw, and render interpolation.
- Integration points: `PlayerState`/`PlayerController` values drive clip mapping. Define deterministic priority for overlapping states: climb/dash/jump-or-fall/run/idle.
- Error paths: map invalid state to idle with one diagnostic; required model load failure stops gameplay initialization rather than quietly drawing cube.

##### Makefile
- What changes: compile animated renderer and package `player.t3dm`, generated `player.animmap`, plus every animation stream declared in asset manifest; keep Make dependency on manifest recipe and generated outputs. Add `tools/patch_t3dm_stream_paths.py` validation/patch step to canonicalize embedded stream paths to `rom:/mdl/<stream-file>` after importer conversion. The animmap is staged at the exact ROM path loaded by `AnimatedModel`.

##### tests/animated_model_runtime_contract.cpp (NEW)
- What changes: host-test pure state-to-clip mapping, priority, loop/restart rules, and transition behavior without tiny3d device dependencies.

##### tests/t3dm_stream_path_smoke.py (NEW)
- What changes: inspect model animation chunk paths and assert each exact ROM path equals both host-manifest `package_path` and runtime animmap path; reject missing, duplicate, absolute host, or non-`rom:/mdl/` stream paths. Test animmap parser/version/errors with host fixtures.

#### Edge cases
- Animation uses fixed simulation tick, not render interpolation delta; visual root motion remains disabled unless explicitly mapped to gameplay movement.
- A clip ending while state persists restarts only for looped states; one-shot dash/jump transitions do not restart each frame.
- Use framebuffer-count buffered skeletons only if needed by current display buffering; free all clip/skeleton/model resources at scene teardown.
- Matrix and animation memory are measured in same 8 MB Expansion Pak arena; no unbounded per-frame allocations.

#### Verification
- Run: `python3 tests/asset_manifest_smoke.py`; `./tests/run_host_tests.sh`; `./compile-rom.sh`.
- Manual/device: capture pre-change and candidate with `tools/capture_baseline.sh`; confirm no player-model FATAL/cube fallback, visible idle/run/jump/dash/climb poses, memory settles across reports, and frame telemetry remains within same-map baseline and 30 Hz frame budget.
- Done: the first visual milestone is met: faithful textured strawberry plus animated Madeline in current map, with Ares logs and before/after telemetry retained under `build/`.

### Inc 6 — Exported-audio conversion and event playback (M)
**Depends on:** 1
**Unblocks:** audio-enabled ROM
**Status:** blocked (2026-09-24; conversion/service/tests implemented, but required licensed WAV exports are absent)
**Done criteria:** declared exported WAV inputs convert to deterministic WAV64, manifest event records map gameplay cues to assets/channel/loop policy, and at least one SFX and looping music event play in Ares.

#### Files to touch

##### assets/og_converted/asset_manifest.json
- What changes: audio entries record external WAV source, WAV64 output, compression/resample recipe, event name, channel class, volume, and whole-file loop policy.
- Data shape: do not encode FMOD bank bytes as runtime data; record source bank/event provenance only as metadata when known.

##### tools/convert_og_assets.py
- What changes: expose manifest-driven WAV64 conversion through existing `convert_audio_wav`; deterministic required/optional handling and output validation.
- Error paths: missing exported source blocks required event package; unsupported codec/loop markers fail with event name.

##### src/user/gameplay/audio/audio_service.hpp and audio_service.cpp (NEW)
- What changes: initialize libdragon mixer after DFS; load required WAV64 assets; provide event API `PlaySfx(AudioEventId)`, `PlayMusic(AudioEventId)`, and `StopMusic()`; apply declared channel/loop/volume policy. Load event metadata from generated ROM file `rom:/audio/events.tsv`; host-side JSON is never parsed on device.
- Data contract: `events.tsv` is deterministic UTF-8 tab-delimited data. Header is `AUDIOEVENTS\t1\t<count>`; each following row has exactly five fields: `event_id`, `rom_path`, `channel_class`, `volume`, `loop`. Channel class is `sfx` or `music`; volume is finite in `[0,1]`; loop is `0` or `1` (whole-file looping only). Reject unsupported version, count mismatch, malformed rows, duplicate IDs/paths, and paths that do not match manifest `package_path`.
- Integration points: `rom_main.cpp` owns service init/shutdown; `GameplayScene::Init()` starts `forsaken_city_theme`, scene teardown stops it; dash-start transition calls `PlaySfx(player_dash)` once per transition. Inc 6 generates/stages `events.tsv` from active manifest entries and includes it in DFS package targets.
- Error paths: missing required event reports exact ROM path and disables that event; mixer/channel exhaustion increments diagnostic counter instead of corrupting playback.

##### src/user/rom_main.cpp and src/user/gameplay/scene/gameplay_scene.cpp
- What changes: initialize audio service after successful DFS init and before scene entry; shut down after scene teardown. Start/stop music at gameplay scene boundaries and trigger dash SFX only on transition into Dashing, not every update tick.
- Data/input gate: before Inc 6 begins, externally export one licensed/approved dash WAV and Forsaken City music WAV into `assets/audio/og_exports/sfx/player_dash.wav` and `assets/audio/og_exports/music/forsaken_city_theme.wav`. Current OG checkout contains only FMOD banks, so do not mark these entries required until source exports are present.
- Error paths: absent external WAVs keep those entries planned/unpackaged and prevent audio-enabled-ROM acceptance; converter fixture WAV validates host conversion logic independently.

##### Makefile and tests/audio_manifest_smoke.py (NEW)
- What changes: include registered WAV64 outputs in DFS and validate event uniqueness, source/output presence, loop policy, and deterministic package list.

#### Edge cases
- FMOD `.bank` extraction remains manual; conversion starts from exported WAV. No third-party bank extraction tool is added.
- Whole-file looping only; if source FMOD event uses loop points/layers, export pre-trimmed sample(s) or record unsupported event explicitly.
- Music resampling/compression profile must preserve acceptable quality and heap use; SFX use short sample profile.
- Audio service initializes only after output/mixer setup and releases samples on shutdown.

#### Verification
- Run: `python3 tests/audio_manifest_smoke.py`; ROM build.
- Manual/device: Ares playback checks for one SFX and one looping track; confirm replay/stop behavior and record ROM size, audio memory, and frame telemetry.
- Done: exported samples and gameplay event mapping work without requiring FMOD runtime or bank parser.

## Cross-cutting verification

- Run host suite after converter/schema or runtime gameplay changes: `./tests/run_host_tests.sh`.
- Run relevant new focused asset validation scripts; host-only checks must not require Blender/libdragon tools unless explicitly labeled toolchain-backed.
- Run `make bake-forsaken-city` after texture/UV changes and validate all registered world materials against LVL2 material IDs and TMEM budgets.
- Run `./compile-rom.sh` after N64 build/runtime changes; build must fail on missing required manifest output or DFS path mismatch.
- Ares is device/emulator oracle: compare same map, spawn, ROM settings, and capture duration before/after. Retain `[memory]`, `[profiler]`, and `[render-phases]`; require no repeated heap growth after streaming stabilizes and no player fallback.
- Verify every runtime `rom:/...` path matches exact `filesystem/...` path and every shipped OG-derived artifact has manifest source provenance.

## Standards / common-mistakes referenced

- Root `AGENTS.md` — preserve gameplay/ROM separation, use libdragon + tiny3d, Expansion Pak target, rebuild ROM after N64-facing changes, protect original-content rights.
- `.agents/common-mistakes/dfs-path-prefix.md` — manifest-generated ROM paths must match `filesystem/` directory layout.
- `.agents/learnings/memory-opt-pooling-and-budget.md` — animation/skeleton allocations share Expansion Pak heap; align pool allocations and bound per-frame growth.
- `.agents/learnings/n64-opt-device-verify-and-telemetry.md` — use fresh Ares capture and telemetry as device evidence; do not use stale documentation heap values.
- No `.agents/standards/` or `.agents/common-mistakes/index.yml` exists in this checkout.
- [tiny3d README](https://github.com/HailToDodongo/tiny3d) and [Fast64 settings](https://github.com/HailToDodongo/tiny3d/blob/main/docs/fast64Settings.md) — importer/material and animation capability boundary.
- [libdragon WAV64 API](https://libdragon.dev/ref/wav64_8c.html) — WAV64 loading, playback, and whole-file loop API for audio increment.

## Open questions (CONSIDER from review)

- None initially. Inc 4 verifies listed source clip inventory against pinned importer output before recording manifest aliases.

## Out of scope

- Replacing OG map bake with T3DM world renderer or changing LVL2/CMSH collision formats.
- Converting all NPCs, cutscene actors, dialogue portraits, UI skins, or every source GLB before first visual milestone.
- Implementing FMOD bank parser, event authoring tool, layered music engine, or full FMOD parameter/transition semantics.
- Public distribution of OG-derived art/audio without separate rights clearance.
