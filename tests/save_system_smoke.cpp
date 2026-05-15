#include <cassert>
#include <cstdio>

#include "../src/user/gameplay/save_system.hpp"

using namespace madeline_cube;

int main() {
    SaveSystem save;

    // Fresh load should return a default block (not valid, but initialized)
    SaveBlock block;
    bool loaded = save.Load(block);
    assert(!loaded);  // No valid save yet
    assert(block.header.magic == kSaveMagic);
    assert(block.header.version == kSaveVersion);

    // Modify and commit
    block.slots[0].level_id = 1;
    block.slots[0].checkpoint_id = 4;
    block.slots[0].strawberry_bits = 0x00042013;
    block.slots[0].deaths = 12;
    block.slots[0].time_frames = 184233;

    bool committed = save.Commit(block);
    assert(committed);

    // Recompute checksum on the block after commit (active_slot flipped)
    block.header.checksum = SaveSystem::ComputeChecksum(block);

    // Validate the committed block
    assert(SaveSystem::Validate(block));

    // Checksum should detect corruption
    SaveBlock corrupted = block;
    corrupted.slots[0].deaths = 99;
    assert(!SaveSystem::Validate(corrupted));

    printf("save_system_smoke: all checks passed\n");
    return 0;
}
