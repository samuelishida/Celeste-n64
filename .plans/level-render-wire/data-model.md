# Data Model

N/A — no runtime schema changes.

## Generated artifacts

- `filesystem/lvl/1-1.lvl` — existing `LVL1` v1 geometry binary.
- `filesystem/lvl/1-1.manifest` — ordered logical material names used by the room.
- `assets/og_converted/textures/*.sprite` — N64-ready material derivatives; first-room level materials are `32x32 RGBA16` and every sprite used by a 3D level manifest must fit TMEM before it can enter the ROM.

The logical material IDs stay stable across baking and rendering. Only the physical texture representation changes when oversized OG source art is conditioned for N64 use.
