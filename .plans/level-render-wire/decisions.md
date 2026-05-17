# Decisions & Assumptions

## D1: First room proves rendering infrastructure, not OG asset parity

- **Context:** The project budget calls for tiny textures, while imported OG materials include `snow_1` at `128x128` and `floor_dirty_concrete` at `256x256`.
- **Decision:** Keep authored material names but generate `32x32 RGBA16` N64-conditioned derivatives for the first room.
- **Consequences:** Visual fidelity yields to reliable ROM behavior for this milestone; later art passes can revisit higher-fidelity materials deliberately.
- **Alternatives rejected:** Ship OG textures unchanged. Ares already proved this crashes on TMEM overflow.

## D2: Every 3D level material must be TMEM-safe before bundling

- **Context:** `rdpq_sprite_upload` requires the full 3D texture to fit TMEM; mere file existence is not enough.
- **Decision:** Add a manifest-level preflight check and treat TMEM fitness as a build contract for level materials.
- **Consequences:** Invalid textures fail before ROM testing instead of during the first textured frame.
- **Alternatives rejected:** Let runtime assertions discover invalid assets. That is slower and obscures which pipeline stage failed.

## D3: Baker remaps material IDs to manifest slots

- **Context:** Raw LVL string IDs and `MaterialCatalog` slots are different index spaces.
- **Decision:** Normalize face material IDs in the baker before writing the LVL.
- **Consequences:** Runtime lookup stays simple; rebuilt LVLs are required after baker changes.
- **Alternatives rejected:** Teach runtime catalog code about baker string-table offsets.

## D4: Per-frame draw path remains the first renderer target

- **Context:** Texture binding is dynamic state and cannot be sealed into the existing all-geometry block.
- **Decision:** Draw faces directly per frame, bind textures by material change, and use `TEX0 x SHADE`.
- **Consequences:** The code is simple enough to validate lighting and material routing; performance is measured before optimization.
- **Alternatives rejected:** Start with per-material recorded blocks. It adds sorting and block management before the base path is proven.

## D5: Face normals come from `t3d_vert_pack_normal()`

- **Context:** T3D expects signed packed 5,6,5 normals.
- **Decision:** Pack flat face normals through the tiny3d helper rather than custom math.
- **Consequences:** Brush faces shade correctly without inventing a second normal encoder.
- **Alternatives rejected:** Manual packing, because negative normals are easy to encode incorrectly.

## Assumptions resolved from code

- `LevelGeometry` capacity covers `1-1` (`402` faces, `2048` vertices). Source: code @ `src/user/gameplay/world/level_loader.hpp`, `src/user/gameplay/world/world.hpp`.
- `1-1` uses six logical materials in manifest order. Source: code @ `tools/bake_map.py`.
- `snow_1` and `floor_dirty_concrete` exceed the TMEM-safe texture budget in source form. Source: source assets @ `Celeste64-og/Content/Textures/`.
- The project already prefers tiny materials over detail-heavy art for the first room. Source: docs @ `docs/asset_budget.md`.

## Open questions (from review)

- Future rooms may need atlases or palette formats instead of one-off downscales once the renderer is proven.
