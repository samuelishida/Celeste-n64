# Asset Inventory

## 3D Models

### Player Character
- **player_base.blend** — Stylized Low Poly Female Base Mesh (WIP) by byzmod3d
  - License: CC0
  - Source: https://opengameart.org/content/stylized-low-poly-female-base-mesh-wip
  - Notes: Clean topology, animation-friendly mesh. Can be modified to match Madeline's style.

### Collectibles
- **strawberry.blend** — Simple Strawberry Model by ChrisTutorials
  - License: CC-BY 4.0 / CC-BY 3.0
  - Source: https://opengameart.org/content/simple-strawberry-model
  - Notes: 421 verts, 701 faces, 746 tris. Simple color materials.

### Environment
- **platformer_houses/** — Free 3D Platformer Houses
  - License: CC0
  - Source: https://opengameart.org/content/free-3d-platformer-houses
  - Contents: 3d-platformer-houses.blend + textures (stone, wood, grass, roof, door, window)
  - Notes: Large textures need downsampling for N64.

- **platformer_pack/** — 2D Platformer Art Pack (sprites)
  - License: Check copyright.txt inside
  - Source: https://opengameart.org/content/free-3d-platformer-art-pack-3-terrain-and-extras
  - Contents: CHARACTER/, TREES/, TILES/ PNG sprites
  - Notes: 2D sprites, can be used for UI or billboard-style decorations.

## Audio

### Sound Effects
- **sfx/jump_09.mp3** — 8-bit Jump Sound
  - License: CC0
  - Source: https://opengameart.org/content/8-bit-jump-1

- **sfx/coin.mp3** — 8-bit Coin Sound
  - License: CC0
  - Source: https://opengameart.org/content/10-8bit-coin-sounds

- **sfx/platformer_sfx.ogg** — 8-bit Platformer SFX Pack Preview
  - License: CC0
  - Source: https://opengameart.org/content/8-bit-platformer-sfx

### Music
- **music/grasslands.mp3** — Grasslands Theme (Platformer Game Music Pack)
  - License: CC-BY 3.0
  - Source: https://opengameart.org/content/platformer-game-music-pack
  - Notes: Loopable background music.

## Conversion Pipeline

### 3D Models → N64
1. Open `.blend` files in Blender
2. Install Fast64 addon
3. Export as GLTF with custom properties
4. Run tiny3d importer: `t3d_model_import input.gltf output.t3d`
5. Load at runtime: `t3d_model_load("rom://models/output.t3d")`

### Textures → N64
1. Convert PNG/JPG to N64 format:
   ```sh
   mksprite --format CI8 --compress 1 texture.png
   ```
2. For 3D model textures, use:
   ```sh
   mksprite --format RGBA16 --compress 1 texture.png
   ```

### Audio → N64
1. Convert MP3/OGG to WAV first (using ffmpeg or similar)
2. Convert WAV to N64 format:
   ```sh
   # For SFX (fast, low CPU):
   audioconv64 --wav-compress 1 sound.wav
   
   # For music (high compression):
   audioconv64 --wav-compress 3 --wav-resample 22050 music.wav
   ```
3. Load at runtime: `wav64_load("rom://audio/sound.wav64")`

## Next Steps

1. **KayKit assets** — The best match for this project. Download from:
   - https://kaylousberg.itch.io/kaykit-platformer (platforms, springs, spikes, coins)
   - https://kaylousberg.itch.io/kaykit-adventurers (rigged characters with animations)
   - https://kaylousberg.itch.io/kaykit-forest (trees, rocks, nature)
   - https://kaylousberg.itch.io/kaykit-character-animations (run, jump, fall, climb animations)

2. **Process existing assets** — Convert the downloaded models through the Fast64 → tiny3d pipeline.

3. **Create asset build rules** — Add Makefile targets for automatic conversion.
