// tests/multichunk_save_smoke.cpp
// Host smoke test: per-chunk strawberry bits + checkpoint room id survive save/load.
#include <cassert>
#include <cstdio>
#include <cstring>
#include "gameplay/save_system.hpp"

using namespace madeline_cube;

int main(int argc, char** argv) {
    printf("[save] testing per-chunk save fields (kSaveVersion=%u)\n", kSaveVersion);

    // 1. Fresh LevelRecord defaults.
    LevelRecord rec = {};
    assert(rec.IsChunkStrawberryCollected(0) == false);
    assert(rec.IsChunkStrawberryCollected(63) == false);
    assert(std::strncmp(rec.GetCheckpointRoom(), "", 16) == 0);
    printf("PASS: fresh record defaults\n");

    // 2. Set/clear chunk strawberry bits.
    rec.SetChunkStrawberryCollected(0, true);
    rec.SetChunkStrawberryCollected(5, true);
    rec.SetChunkStrawberryCollected(63, true);
    assert(rec.IsChunkStrawberryCollected(0) == true);
    assert(rec.IsChunkStrawberryCollected(5) == true);
    assert(rec.IsChunkStrawberryCollected(63) == true);
    assert(rec.IsChunkStrawberryCollected(1) == false);
    printf("PASS: set/clear chunk strawberry bits (0,5,63)\n");

    // 3. Clear a bit.
    rec.SetChunkStrawberryCollected(5, false);
    assert(rec.IsChunkStrawberryCollected(5) == false);
    assert(rec.IsChunkStrawberryCollected(0) == true);  // others unchanged
    printf("PASS: clear single bit preserves others\n");

    // 4. Checkpoint room id.
    rec.SetCheckpointRoom("cell_03_05");
    assert(std::strncmp(rec.GetCheckpointRoom(), "cell_03_05", 16) == 0);
    rec.SetCheckpointRoom("cell_n02_00");
    assert(std::strncmp(rec.GetCheckpointRoom(), "cell_n02_00", 16) == 0);
    printf("PASS: checkpoint room id set/get\n");

    // 5. SaveBlock round-trip with per-chunk fields.
    SaveBlock block = {};
    block.header.magic = kSaveMagic;
    block.header.version = kSaveVersion;
    block.slots[0].level_id = 1;
    block.slots[0].SetChunkStrawberryCollected(7, true);
    block.slots[0].SetChunkStrawberryCollected(12, true);
    block.slots[0].SetCheckpointRoom("cell_01_02");
    block.slots[0].deaths = 3;
    block.slots[0].time_frames = 12345;

    // Compute checksum (simulate Commit).
    SaveBlock block2 = block;
    block2.header.checksum = SaveSystem::ComputeChecksum(block2);
    assert(SaveSystem::Validate(block2) == true);
    printf("PASS: SaveBlock round-trip with per-chunk fields\n");

    // 6. Verify fields survive a "load" (copy + validate).
    SaveBlock loaded = block2;
    assert(loaded.slots[0].IsChunkStrawberryCollected(7) == true);
    assert(loaded.slots[0].IsChunkStrawberryCollected(12) == true);
    assert(loaded.slots[0].IsChunkStrawberryCollected(0) == false);
    assert(std::strncmp(loaded.slots[0].GetCheckpointRoom(), "cell_01_02", 16) == 0);
    assert(loaded.slots[0].deaths == 3);
    printf("PASS: fields survive copy + validate\n");

    // 7. Out-of-range chunk index is safe.
    rec.SetChunkStrawberryCollected(-1, true);
    rec.SetChunkStrawberryCollected(999, true);
    assert(rec.IsChunkStrawberryCollected(-1) == false);
    assert(rec.IsChunkStrawberryCollected(999) == false);
    printf("PASS: out-of-range chunk index is safe\n");

    printf("\nAll multichunk_save_smoke tests passed.\n");
    return 0;
}
