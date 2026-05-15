# Data model

## Entities & relationships

The validation build uses fixed binary records on cartridge storage plus prototype scene data compiled into the ROM.

```c
typedef struct {
    uint32_t magic;          // "C64N"
    uint16_t version;        // starts at 1
    uint16_t checksum;
    uint8_t active_slot;     // 0 or 1
    uint8_t language_id;
    uint8_t music_volume;    // 0..10
    uint8_t sfx_volume;      // 0..10
    uint8_t flags;           // bit0 z-guide, bit1 timer, bits2-3 invert camera
} save_header_t;

typedef struct {
    uint16_t level_id;
    uint16_t checkpoint_id;
    uint32_t strawberry_bits;     // 30 used by Forsaken City
    uint16_t completed_submap_bits;
    uint16_t script_flag_bits;
    uint16_t deaths;
    uint32_t time_frames;
} level_record_t;

typedef struct {
    save_header_t header;
    level_record_t slots[2];
} save_block_t;

typedef struct {
    uint16_t map_id;
    uint16_t chunk_count;
    uint32_t chunk_table_offset;
    uint32_t actor_table_offset;
    uint32_t collision_table_offset;
} baked_map_header_t;

typedef struct {
    uint16_t placeholder_id;
    uint8_t semantic_kind;   // player, berry, refill, npc, prop, hazard...
    uint8_t primitive_kind;  // box, sphere, capsule, cone, quad...
    uint16_t color_id;
    uint16_t scale_id;
} placeholder_asset_t;
```

- `save_block_t` stores one active `level_record_t` slot and one previous slot for rollback-safe writes.
- `baked_map_header_t` points to prototype geometry, actor, and collision tables for graybox rooms.
- `placeholder_asset_t` names what an authored asset means before any final model or texture conversion exists.

## Constraints & indexes

- `magic` must equal `C64N`; unknown values reject the save block.
- `version` is monotonic; v1 readers may only load v1, later readers must define explicit migration code.
- `level_id` is unique per record; v1 ships one record for Forsaken City.
- `strawberry_bits` must not set bits above the authored strawberry count for the level.
- Build tooling must fail if authored content exceeds the bit capacity of `strawberry_bits`, `completed_submap_bits`, or `script_flag_bits`.
- `music_volume` and `sfx_volume` are clamped to `0..10`.
- `time_frames` stores elapsed gameplay frames at the fixed simulation rate.
- Placeholder IDs and map IDs are dense numeric IDs generated at build time from semantic keys such as `pickup_strawberry`, `npc_granny`, and `hazard_spike`; lookup tables are sorted by ID for binary search.
- Hot runtime lookups: actor table by type, collision chunks by chunk ID, placeholders by `placeholder_id`.

## Query patterns

1. Load the newest valid save slot at boot by checksum, then read settings and current level record.
2. On strawberry pickup, set one bit in `strawberry_bits`, update checkpoint/time, and commit the inactive slot before flipping `active_slot`.
3. On room load, resolve `map_id` to its baked chunk table, then pull only visible geometry, collision, and actor rows.
4. On scene transition, resolve placeholder rows for the next map and bind the required primitive geometry definitions.
5. During world update, query collision chunks around the player and actor spawns for the active map.

## Sample rows

`save_header_t`

| magic | version | checksum | active_slot | language_id | music_volume | sfx_volume | flags |
|---|---:|---:|---:|---:|---:|---:|---:|
| `C64N` | 1 | `0x6A31` | 1 | 0 | 10 | 10 | `0b00000011` |

`level_record_t`

| level_id | checkpoint_id | strawberry_bits | completed_submap_bits | script_flag_bits | deaths | time_frames |
|---:|---:|---:|---:|---:|---:|---:|
| 0 | 4 | `0x00042013` | `0x0007` | `0x0003` | 12 | 184233 |

`baked_map_header_t`

| map_id | chunk_count | chunk_table_offset | actor_table_offset | collision_table_offset |
|---:|---:|---:|---:|---:|
| 0 | 18 | `0x00124000` | `0x0012A600` | `0x00131A00` |

`placeholder_asset_t`

| placeholder_id | semantic_kind | primitive_kind | color_id | scale_id |
|---:|---|---|---:|---:|
| 42 | strawberry | sphere | 3 | 2 |

## Migration plan

1. Ship v1 as a new save format; no automatic import from the PC `save.json` is planned.
2. Reserve `version` and two save slots from the first build so later versions can migrate in place.
3. Future migrations read the newest valid old slot, transform into a new in-memory struct, write the inactive slot, verify checksum, then flip `active_slot`.
4. Writes are tiny and bounded; lock duration is one EEPROM/SRAM commit, not a gameplay-loop stall if scheduled during transitions.
5. If later authored content exceeds any v1 bitfield, that release must bump the save version and ship an explicit migration.

## Backwards-compatibility window

- Runtime reads only the current version plus explicitly supported older versions.
- A write never destroys the last valid slot before the replacement slot verifies.
- PC save compatibility is N/A - separate platform and storage model.

## Backfill

- N/A for v1 - the game ships with one authored level and no existing N64 save population.
- Future content additions require a table-driven default row for each new level ID so older saves load with zeroed progression for new content.

## Rollback

1. If the inactive slot write fails checksum verification, keep the previous active slot.
2. If a later version migration fails, boot from the newest valid old-version slot and show a recoverable save warning.
3. If a graybox data build is bad, roll back the ROM build; there is no runtime mutation to undo.
