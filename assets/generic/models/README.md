# Models

Source models for the project live here.

## Downloaded Assets

### Player Character
- `player_base.blend` — Stylized Low Poly Female Base Mesh (CC0)
  - Source: https://opengameart.org/content/stylized-low-poly-female-base-mesh-wip
  - 667 KB, clean topology, animation-friendly

### Collectibles
- `strawberry.blend` — Simple Strawberry Model (CC-BY 4.0)
  - Source: https://opengameart.org/content/simple-strawberry-model
  - 421 verts, 701 faces, 746 tris

### Environment
- `platformer_houses/` — Free 3D Platformer Houses (CC0)
  - Source: https://opengameart.org/content/free-3d-platformer-houses
  - Includes: 3d-platformer-houses.blend + stone/wood/grass/roof textures

## Recommended Downloads (Manual)

The following are the best match for this project but require manual download from itch.io:

1. **KayKit — Platformer Pack** (FREE, CC0)
   - https://kaylousberg.itch.io/kaykit-platformer
   - 120+ models: platforms, springs, spikes, coins, flags, pipes
   - Includes .GLTF format (directly importable to tiny3d)

2. **KayKit — Adventurers** (FREE, CC0)
   - https://kaylousberg.itch.io/kaykit-adventurers
   - 5 rigged & animated characters
   - Basic movement animations included

3. **KayKit — Character Animations** (FREE, CC0)
   - https://kaylousberg.itch.io/kaykit-character-animations
   - 25+ humanoid animations: idle, run, jump, fall, climb, dash

4. **KayKit — Forest Nature Pack** (FREE, CC0)
   - https://kaylousberg.itch.io/kaykit-forest
   - Trees, rocks, grass, modular terrain

## Conversion Pipeline

### Blender → Fast64 → GLTF → tiny3d

1. Open `.blend` file in Blender
2. Install Fast64 addon (https://github.com/Fast-64/fast64)
3. Configure materials using N64 combiner settings
4. Export as GLTF with **custom properties enabled**
5. Run tiny3d importer:
   ```sh
   t3d_model_import input.gltf output.t3d
   ```
6. Load at runtime:
   ```c
   t3d_model_load("rom://models/output.t3d");
   ```

## N64 Considerations

- **Vertex count:** Keep under ~2000 verts per model for performance
- **Textures:** Use single atlas texture per asset pack when possible
- **Materials:** Prefer vertex colors over complex textures
- **Animation:** Use skinned meshes with 1 bone per vertex (tiny3d limitation)

Milestone 0:

- `player_cube`
- `platform_test`
- `strawberry_cube`

Milestone 2 onward:

- low-poly player model
- compact authored test room

