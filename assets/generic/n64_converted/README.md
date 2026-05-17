# N64 Converted Assets

This directory contains all assets converted to Nintendo 64 compatible formats.

## Audio

### Sound Effects (`audio/sfx/`)
| File | Original | Format | Size |
|---|---|---|---|
| `n64_sfx_coin.wav64` | coin.mp3 | VADPCM (level 1) | 36 KB |
| `n64_sfx_jump_09.wav64` | jump_09.mp3 | VADPCM (level 1) | 4.5 KB |
| `n64_sfx_platformer_sfx.wav64` | platformer_sfx.ogg | VADPCM (level 1) | 82 KB |

### Music (`audio/music/`)
| File | Original | Format | Size |
|---|---|---|---|
| `n64_music_grasslands.wav64` | grasslands.mp3 | Opus (level 3) | 161 KB |

## Textures

All textures converted to N64 RGBA16 format with libdragon compression level 1.

| File | Original | Size |
|---|---|---|
| `n64_tex_door_1.sprite` | door_1.jpg | 207 KB |
| `n64_tex_grass_2.sprite` | grass_2.jpg | 1.3 MB |
| `n64_tex_grass_2_norm.sprite` | grass_2_norm.jpg | 1.9 MB |
| `n64_tex_house_wall.sprite` | house_wall.JPG | 670 KB |
| `n64_tex_house_wall_norm.sprite` | house_wall_norm.JPG | 937 KB |
| `n64_tex_house_wall_red.sprite` | house_wall_red.JPG | 900 KB |
| `n64_tex_house_wall_red_norm.sprite` | house_wall_red_norm.JPG | 1.4 MB |
| `n64_tex_roof_black.sprite` | roof_black.jpg | 1.6 MB |
| `n64_tex_roof_black_norm.sprite` | roof_black_norm.jpg | 1.5 MB |
| `n64_tex_roof_red.sprite` | roof_red.jpg | 1.3 MB |
| `n64_tex_roof_red_norm.sprite` | roof_red_norm.jpg | 752 KB |
| `n64_tex_stonetiles_002_diff.sprite` | stonetiles_002_diff.jpg | 47 KB |
| `n64_tex_stonetiles_002_norm.sprite` | stonetiles_002_norm.jpg | 59 KB |
| `n64_tex_stonetiles_003_diff.sprite` | stonetiles_003_diff.jpg | 68 KB |
| `n64_tex_stonetiles_003_norm.sprite` | stonetiles_003_norm.jpg | 94 KB |
| `n64_tex_window_1.sprite` | window_1.JPG | 19 KB |
| `n64_tex_window_2.sprite` | window_2.JPG | 66 KB |
| `n64_tex_window_3.sprite` | window_3.JPG | 172 KB |
| `n64_tex_wood4.sprite` | wood4.jpg | 203 KB |
| `n64_tex_wood4-norm.sprite` | wood4-norm.jpg | 471 KB |
| `n64_tex_wood5.sprite` | wood5.jpg | 267 KB |
| `n64_tex_wood5-norm.sprite` | wood5-norm.jpg | 467 KB |

## Models

### Blocky Characters (`models/`)
**Source:** Kenney Blocky Characters (CC0)
- https://opengameart.org/content/blocky-characters
- 18 blocky/Roblox-style characters with simple geometry

| File | Size | Notes |
|---|---|---|
| `character-a.model64` | 2.5 KB | Base mesh + 23 KB animation |
| `character-b.model64` | 2.5 KB | Variant B |
| `character-c.model64` | 2.5 KB | Variant C |
| `character-d.model64` | 2.5 KB | Variant D |

**Format:** libdragon `model64` (native N64 format)
**Usage:**
```c
model64_t* model = model64_load("rom://models/character-a.model64");
model64_draw(model);
```

### Pending Manual Conversion (tiny3d)
The following `.blend` files need Blender → GLTF → `.t3d` conversion:
- `player_base.blend` → needs Blender → GLTF → `.t3d`
- `strawberry.blend` → needs Blender → GLTF → `.t3d`
- `platformer_houses/3d-platformer-houses.blend` → needs Blender → GLTF → `.t3d`

**Pipeline:**
1. Open `.blend` in Blender
2. Install Fast64 addon
3. Export as GLTF with custom properties
4. Run: `gltf_to_t3d input.gltf output.t3d`

## Character Textures (`textures/`)

Converted from Kenney Blocky Characters texture atlases.

| File | Original | Size | Description |
|---|---|---|---|
| `texture-a.sprite` | texture-a.png | ~15 KB | Character A atlas |
| `texture-b.sprite` | texture-b.png | ~15 KB | Character B atlas |
| `texture-c.sprite` | texture-c.png | ~15 KB | Character C atlas |
| `texture-d.sprite` | texture-d.png | ~15 KB | Character D atlas |
| `texture-e.sprite` | texture-e.png | ~15 KB | Character E atlas |
| `texture-f.sprite` | texture-f.png | ~15 KB | Character F atlas |
| `texture-g.sprite` | texture-g.png | ~15 KB | Character G atlas |
| `texture-h.sprite` | texture-h.png | ~15 KB | Character H atlas |
| `texture-i.sprite` | texture-i.png | ~15 KB | Character I atlas |
| `texture-j.sprite` | texture-j.png | ~15 KB | Character J atlas |
| `texture-k.sprite` | texture-k.png | ~15 KB | Character K atlas |
| `texture-l.sprite` | texture-l.png | ~15 KB | Character L atlas |
| `texture-m.sprite` | texture-m.png | ~15 KB | Character M atlas |
| `texture-n.sprite` | texture-n.png | ~15 KB | Character N atlas |
| `texture-o.sprite` | texture-o.png | ~15 KB | Character O atlas |
| `texture-p.sprite` | texture-p.png | ~15 KB | Character P atlas |
| `texture-q.sprite` | texture-q.png | ~15 KB | Character Q atlas |
| `texture-r.sprite` | texture-r.png | ~15 KB | Character R atlas |

**Format:** CI8 (256-color palettized) with libdragon compression level 1
**Usage:**
```c
sprite_t* tex = sprite_load("rom://textures/texture-a.sprite");
rdpq_sprite_upload(TILE0, tex, NULL);
```

## Usage in Code

```c
// Load texture
sprite_t* spr = sprite_load("rom://textures/n64_tex_grass_2.sprite");
rdpq_sprite_upload(TILE0, spr, NULL);

// Load SFX
wav64_load("rom://audio/sfx/n64_sfx_jump_09.wav64");

// Load music
wav64_load("rom://audio/music/n64_music_grasslands.wav64");

// Load model (after manual conversion)
t3d_model_load("rom://models/player.t3d");
```

## Makefile Integration

Add to Makefile for automatic asset bundling:
```makefile
ASSETS = \
    assets/n64_converted/textures/*.sprite \
    assets/n64_converted/audio/sfx/*.wav64 \
    assets/n64_converted/audio/music/*.wav64 \
    assets/n64_converted/models/*.model64

# Assets are automatically bundled into ROM filesystem
```

## Conversion Tools Used

| Tool | Source | Purpose |
|---|---|---|
| `mksprite` | libdragon | PNG/JPG → `.sprite` (RGBA16/CI8) |
| `audioconv64` | libdragon | WAV/MP3/OGG → `.wav64` (VADPCM/Opus) |
| `mkmodel` | libdragon | GLB/OBJ/FBX → `.model64` (native N64) |
| `gltf_to_t3d` | tiny3d | GLTF → `.t3d` (tiny3d format) |
| `convert` | ImageMagick | Format conversion (JPG → PNG) |

## Source Assets

| Asset Pack | License | Location |
|---|---|---|
| Kenney Blocky Characters | CC0 | `assets/models/kenney_blocky/` |
| Platformer SFX | CC0 | `assets/audio/sfx/` |
| Grasslands Music | CC0 | `assets/audio/music/` |
| Platformer Houses | CC0 | `assets/models/platformer_houses/` |
| Player Base | CC0 | `assets/models/player_base/` |
| Strawberry | CC0 | `assets/models/strawberry/` |

## Notes

- **Blender required** for `.blend` → `.t3d` conversion (tiny3d pipeline)
- **mksprite** only accepts PNG input; use ImageMagick to convert JPG first
- **gltf_to_t3d** requires Fast64-exported GLTF with custom properties
- All converted assets are optimized for N64 hardware constraints
