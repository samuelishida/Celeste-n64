#include "save_system.hpp"

#include <cstring>

namespace madeline_cube {

namespace {

// Simple CRC-16 for checksums.
uint16_t Crc16(const uint8_t* data, size_t length) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < length; ++i) {
        crc ^= static_cast<uint16_t>(data[i]) << 8;
        for (int j = 0; j < 8; ++j) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ 0x1021;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

}  // namespace

SaveSystem::SaveSystem() {}

uint16_t SaveSystem::ComputeChecksum(const SaveBlock& block) {
    SaveBlock copy = block;
    copy.header.checksum = 0;
    return Crc16(reinterpret_cast<const uint8_t*>(&copy), sizeof(copy));
}

bool SaveSystem::Validate(const SaveBlock& block) {
    if (block.header.magic != kSaveMagic) {
        return false;
    }
    if (block.header.version != kSaveVersion) {
        return false;
    }
    const uint16_t expected = ComputeChecksum(block);
    return block.header.checksum == expected;
}

bool SaveSystem::Load(SaveBlock& out_block) {
    SaveBlock slots[2];
    bool valid[2] = {false, false};

    for (int i = 0; i < 2; ++i) {
        slots[i] = SaveBlock{};
        valid[i] = Validate(slots[i]);
    }

    int best_slot = -1;
    if (valid[0] && valid[1]) {
        best_slot = (slots[0].header.active_slot >= slots[1].header.active_slot) ? 0 : 1;
    } else if (valid[0]) {
        best_slot = 0;
    } else if (valid[1]) {
        best_slot = 1;
    }

    if (best_slot >= 0) {
        out_block = slots[best_slot];
        return true;
    }

    out_block = SaveBlock{};
    out_block.header.magic = kSaveMagic;
    out_block.header.version = kSaveVersion;
    out_block.header.checksum = ComputeChecksum(out_block);
    return false;
}

bool SaveSystem::Commit(const SaveBlock& block) {
    SaveBlock to_write = block;

    uint8_t inactive_slot = (to_write.header.active_slot == 0) ? 1 : 0;
    to_write.header.active_slot = inactive_slot;

    to_write.header.checksum = ComputeChecksum(to_write);
    return Validate(to_write);
}

bool SaveSystem::ReadRaw(SaveBlock& block) {
    block = SaveBlock{};
    return false;
}

bool SaveSystem::WriteRaw(const SaveBlock& block, uint8_t slot) {
    return true;
}

}  // namespace madeline_cube
