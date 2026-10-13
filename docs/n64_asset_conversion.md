# N64 asset conversion: animated flat-color Madeline

The player is a rigged tiny3d model with flat per-triangle vertex colors.
Runtime animation selects clips from gameplay state; the source atlas is used
only during offline color baking.

## Source and outputs

- Source: `Celeste64-og/Content/Models/player.glb`; embedded image 0 is sampled
  offline to bake per-face `COLOR_0` values. The prepared GLB preserves skin
  weights and animation clips but contains no image, texture, or UV dependency.
  The report records the source image hash.
- The source `Hair` material uses grayscale texels, so its baked values receive
  a red-orange tint while retaining the atlas's light and dark shading.
- Prepared GLB: `build/n64-prepared/player.glb`.
- Runtime model: `rom:/mdl/player.t3dm`.
- Runtime animation map: `rom:/mdl/player.animmap`; eleven `.sdata` streams are
  packaged alongside the model.
- Runtime texture: none.
- Active clips: `Idle`, `Run`, `Jump`, `Dash`, `Climb.Idle`, and `Climb.Up`.
- Model profile: 17 joints, flat normals, two material slots (`Player`,
  `Hair`), shade-only combiner with opaque vertex colors and shaded/depth
  flags, no images or UVs, 4096-triangle and 128 KiB T3DM budgets.
- Runtime transform: camera-relative interpolated player position, facing yaw,
  importer base scale `64.0`, runtime matrix scale `0.07735`, feet pivot offset
  `1.466675` world units.
- Source `Armature.translation.x = 0.1236680299` is migrated into skin-root
  bind/animation translations and restored as a facing-relative outer model
  offset (`0.6122062` world units at presentation scale 1). Tiny3D's importer
  keeps only the first joint influence, so converter emits manifest-packaged
  `rom:/mdl/player.wsk` (WSK1, 110,752 bytes) with all four source joints and
  normalized weights per GLB vertex. Runtime maps 2,492 source vertices to
  2,688 packed T3DM vertices and CPU-skins into dynamic vertex buffers. Bind
  positions match the packed model exactly; animated pose and pixel parity
  remain unproven. CPU cost is high (Ares idle mean ≈21.7 ms; emulator proxy,
  not N64 hardware timing). Current 3,600-frame mallinfo capture reports arena
  total6,403,784 / used6,369,896 / free33,888 bytes; see
  `docs/memory_budget.md` for limits and historical comparison.
- Wall presentation clearance uses the full horizontal radius to find nearby
  faces, then projects the packed T3DM vertex bounds through Madeline's facing
  direction. This avoids moving the smaller model an extra few units away from
  a wall just because the circumscribed box radius includes empty corners.

## Repeating world texture filtering

Opaque repeating map textures reduce to a 32x32 RGBA16 base tile with a box
prefilter, then receive a box-filter mip chain and bilinear/trilinear sampling.
This avoids baking source-image aliasing into every mip level when images such
as 256x256 `floor_dirty_concrete` are reduced to the N64 tile size. The full
chain remains within the 4 KiB TMEM limit. Straight-alpha decals stay nearest
and unmipped until the pipeline supports alpha-aware filtering.

The converter honors each profile's `resize_filter`; `tests/texture_profile_smoke.py`
uses a checkerboard reduction regression to ensure repeating textures are
prefiltered. After changing these profiles, regenerate each active texture
with `python3 tools/convert_og_assets.py --manifest assets/og_converted/asset_manifest.json --asset-id <id>`,
then rebuild the ROM so its DFS contains the updated sprites.

Build conversion with the locked asset interpreter after `./setup.sh`:

```sh
./setup.sh
./compile-rom.sh
```

Manifest validation and host checks:

```sh
python3 tools/asset_manifest.py --manifest assets/og_converted/asset_manifest.json --check
./tests/run_host_tests.sh
```

## Runtime validation

`./compile-rom.sh` builds the ROM and the DFS contains the model, animation map,
all eleven streams, and `filesystem/mdl/player.wsk` without
`filesystem/tex/player.sprite`. Host inspection checks the shade combiner,
opaque vertex colors, skinning chunks, animation chunks, sidecar paths, and the
128 KiB model budget. Ares confirms seven clips and weighted-skin runtime load;
physics route telemetry passes. Rendered-pose / pixel visual review has not
been run for this animated build.
