#pragma once

#include <cstdint>

namespace madeline_cube {

// Save block layout matching data-model.md
// Stored in EEPROM or SRAM with dual-slot rollback safety.

constexpr uint32_t kSaveMagic = 0x4336344E;  // "C64N"
constexpr uint16_t kSaveVersion = 2;  // bumped for per-chunk save fields

struct SaveHeader {
    uint32_t magic = kSaveMagic;
    uint16_t version = kSaveVersion;
    uint16_t checksum = 0;
    uint8_t active_slot = 0;
    uint8_t language_id = 0;
    uint8_t music_volume = 10;
    uint8_t sfx_volume = 10;
    uint8_t flags = 0x03;  // bit0 z-guide, bit1 timer
};

static constexpr int kMaxChunkBits = 64;  // matches MapSpec::kMaxRooms

struct LevelRecord {
    uint16_t level_id = 0;
    uint16_t checkpoint_id = 0;
    uint32_t strawberry_bits = 0;           // legacy per-level bits (kept for B-side compat)
    uint16_t completed_submap_bits = 0;
    uint16_t script_flag_bits = 0;
    uint16_t deaths = 0;
    uint32_t time_frames = 0;
    // Per-chunk save for map-packs (Forsaken City A-side).
    uint64_t chunk_strawberry_bits[kMaxChunkBits / 64] = {};  // 64-bit bitfield per chunk (64 chunks max)
    char checkpoint_room_id[16] = {};  // which room to respawn in for this level

    // Per-chunk strawberry helpers (chunk_index must be 0..63).
    bool IsChunkStrawberryCollected(int chunk_index) const {
        if (chunk_index < 0 || chunk_index >= kMaxChunkBits) return false;
        return (chunk_strawberry_bits[chunk_index / 64] >> (chunk_index % 64)) & 1ULL;
    }
    void SetChunkStrawberryCollected(int chunk_index, bool collected = true) {
        if (chunk_index < 0 || chunk_index >= kMaxChunkBits) return;
        if (collected) {
            chunk_strawberry_bits[chunk_index / 64] |= (1ULL << (chunk_index % 64));
        } else {
            chunk_strawberry_bits[chunk_index / 64] &= ~(1ULL << (chunk_index % 64));
        }
    }

    // Checkpoint room helpers.
    void SetCheckpointRoom(const char* room_id) {
        if (!room_id) { checkpoint_room_id[0] = '\0'; return; }
        for (int i = 0; i < 15 && room_id[i]; ++i) checkpoint_room_id[i] = room_id[i];
        checkpoint_room_id[15] = '\0';
    }
    const char* GetCheckpointRoom() const { return checkpoint_room_id; }
};

struct SaveBlock {
    SaveHeader header;
    LevelRecord slots[2];
};

// Platform-agnostic save interface.
// N64 implementation uses EEPROM/SRAM via libdragon.
// Host implementation uses a file.
class SaveSystem {
public:
    SaveSystem();

    // Load the newest valid slot from storage.
    bool Load(SaveBlock& out_block);

    // Save to the inactive slot, then flip active_slot.
    bool Commit(const SaveBlock& block);

    // Compute checksum over the save data (excluding the checksum field itself).
    static uint16_t ComputeChecksum(const SaveBlock& block);

    // Validate a loaded block.
    static bool Validate(const SaveBlock& block);

private:
    bool ReadRaw(SaveBlock& block);
    bool WriteRaw(const SaveBlock& block, uint8_t slot);
};

}  // namespace madeline_cube
