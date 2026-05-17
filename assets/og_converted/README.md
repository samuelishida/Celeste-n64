# OG Converted Assets

This directory contains a 1:1 conversion of all convertible assets from
`Celeste64-og/Content/` into Nintendo 64 compatible formats.

## Conversion Date
2026-05-16

## Toolchain
- libdragon `preview` (N64_INST=/tmp/n64-toolchain-root/opt/libdragon)
- tiny3d
- `mksprite`, `gltf_to_t3d`, `mkfont`, `audioconv64`

## Conversion Script
Run from repo root:
```sh
N64_INST=/tmp/n64-toolchain-root/opt/libdragon \
PATH=/tmp/n64-toolchain-root/opt/libdragon/bin:$PATH \
python3 tools/convert_og_assets.py
```

---

## Models (`models/`)

| File | Source | Status | Notes |
|---|---|---|---|
| `antenna.t3dm` | antenna.glb | ✅ OK | |
| `badeline.t3dm` | badeline.glb | ❌ FAILED | Armature/skin transform error |
| `bucket.t3dm` | bucket.glb | ✅ OK | |
| `bush1.t3dm` | bush1.glb | ✅ OK | |
| `car_collider.t3dm` | car_collider.glb | ✅ OK | |
| `car_mirrors.t3dm` | car_mirrors.glb | ✅ OK | |
| `car_top.t3dm` | car_top.glb | ✅ OK | |
| `car_wheels.t3dm` | car_wheels.glb | ✅ OK | |
| `coin.t3dm` | coin.glb | ✅ OK | |
| `feather.t3dm` | feather.glb | ✅ OK | |
| `flag_off.t3dm` | flag_off.glb | ✅ OK | |
| `flag_on.t3dm` | flag_on.glb | ❌ FAILED | Armature/skin transform error |
| `granny.t3dm` | granny.glb | ❌ FAILED | Armature/skin transform error |
| `grass1.t3dm` | grass1.glb | ✅ OK | |
| `hydrant.t3dm` | hydrant.glb | ✅ OK | |
| `logo.t3dm` | logo.glb | ✅ OK | |
| `player.t3dm` | player.glb | ❌ FAILED | Armature/skin transform error |
| `refill_gem.t3dm` | refill_gem.glb | ✅ OK | |
| `refill_gem_double.t3dm` | refill_gem_double.glb | ✅ OK | |
| `sign.t3dm` | sign.glb | ✅ OK | |
| `sign1.t3dm` | sign1.glb | ✅ OK | |
| `sign2.t3dm` | sign2.glb | ✅ OK | |
| `sign3.t3dm` | sign3.glb | ✅ OK | |
| `sign4.t3dm` | sign4.glb | ✅ OK | |
| `spike.t3dm` | spike.glb | ✅ OK | |
| `spike_ball.t3dm` | spike_ball.glb | ✅ OK | |
| `spring_board.t3dm` | spring_board.glb | ✅ OK | + `.sdata` animation file |
| `strawberry.t3dm` | strawberry.glb | ✅ OK | |
| `tape_1.t3dm` | tape_1.glb | ✅ OK | |
| `tape_2.t3dm` | tape_2.glb | ✅ OK | |
| `theo.t3dm` | theo.glb | ❌ FAILED | Armature/skin transform error |
| `trafic1.t3dm` | trafic1.glb | ✅ OK | |
| `tree1.t3dm` | tree1.glb | ✅ OK | |

**Failed models:** The 5 rigged characters (`badeline`, `flag_on`, `granny`, `player`, `theo`) fail because `gltf_to_t3d` rejects GLBs where armature ancestors have significant transforms. These need to be re-exported from Blender with applied transforms before the GLB export, or processed through the Fast64 → GLTF pipeline.

---

## Textures (`textures/`)

All 41 root-level PNG textures converted to `RGBA16` `.sprite` format.

Additional subdirectories:
- `textures/overworld/` — 3 files (overlay, strawberry, vignette)
- `textures/postcards/` — 2 files (ForsakenCity, back)
- `textures/skyboxes/` — 4 files (bsides_0/1/2, city)

---

## Sprites (`sprites/`)

21 gameplay sprites converted to `CI8` (palette) `.sprite` format for smaller size.

---

## Face Textures (`faces/`)

64 character dialogue portrait textures converted to `RGBA16` `.sprite` format.

Subdirectories preserved:
- `faces/baddy/` — 16 files
- `faces/granny/` — 6 files
- `faces/madeline/` — 14 files
- `faces/theo/` — 22 files
- `faces/signpost00.sprite` — 1 file

---

## Fonts (`fonts/`)

| File | Source | Status |
|---|---|---|
| `Renogare.font64` | Renogare.otf | ✅ OK |

---

## Control Sprites (`sprites/controls/`)

10 platform-specific button prompt sprites (cancel/confirm for Switch, PC, PS4, PS5, Xbox).

---

## Maps (`maps/`)

12 `.map` files copied as-is (Trenchbroom format, parsed at runtime):
- `1.map`, `1-1.map` … `1-10.map`, `Palette.map`

---

## Config (`config/`)

| File | Source | Status |
|---|---|---|
| `Levels.json` | OG | ✅ copied |
| `GameConfig.cfg` | OG | ✅ copied |
| `Celeste64.fgd` | OG | ✅ copied |

---

## Audio

**SKIPPED** — The OG audio is stored in FMOD Studio `.bank` files (`Master.bank`, `music.bank`, `sfx.bank`). These are proprietary RIFF/FEV containers and cannot be batch-converted without FMOD Studio or a compatible extraction tool.

If you extract the banks to WAV/MP3, place them under:
- `audio/sfx/` — run `audioconv64 --wav-compress 1`
- `audio/music/` — run `audioconv64 --wav-compress 3 --wav-resample 22050`

---

## Known Issues / Next Steps

1. **Rigged character models** (`badeline`, `granny`, `player`, `theo`, `flag_on`) need Blender re-export with applied transforms, or Fast64 pipeline.
2. **Audio** requires FMOD bank extraction before N64 conversion.
3. **Materials** were ignored during model conversion (`--ignore-materials`) because the OG GLBs lack Fast64 custom properties. Runtime code will need to assign materials manually.
4. **Transforms** were ignored (`--ignore-transforms`) to avoid object transform errors. Some models may need position/scale adjustment at load time.
