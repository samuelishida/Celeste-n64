#include <cassert>

#include "gameplay/world/hazard_query.hpp"

using namespace madeline_cube;

int main() {
    HazardVolumeSpec death{};
    death.kind = kHazardDeathBlock;
    death.bounds = {{-1.0f, 2.0f, 3.0f}, {4.0f, 5.0f, 6.0f}};
    assert(HazardKillsAtPoint(death, {-1.0f, 2.0f, 3.0f}, {}));
    assert(HazardKillsAtPoint(death, {4.0f, 5.0f, 6.0f}, {}));
    assert(!HazardKillsAtPoint(death, {4.001f, 5.0f, 6.0f}, {}));

    HazardVolumeSpec spike{};
    spike.kind = kHazardSpikeBlock;
    spike.bounds = death.bounds;
    spike.spike_direction = {1.0f, 0.0f, 0.0f};
    const Vec3 inside = {0.0f, 3.0f, 4.0f};
    assert(!HazardKillsAtPoint(spike, inside, {10.0f, 0.0f, 0.0f}));
    assert(HazardKillsAtPoint(spike, inside, {-10.0f, 0.0f, 0.0f}));
    assert(HazardKillsAtPoint(spike, inside, {0.0f, 10.0f, 0.0f}));
    assert(!HazardKillsAtPoint(spike, inside, {}));
    assert(!HazardKillsAtPoint(spike, {5.0f, 3.0f, 4.0f}, {-1.0f, 0.0f, 0.0f}));

    MapSpecV2 map{};
    map.hazard_count = 1;
    map.hazards[0] = spike;
    assert(AnyHazardKillsAtPoint(map, inside, {-1.0f, 0.0f, 0.0f}));
    assert(!AnyHazardKillsAtPoint(map, inside, {1.0f, 0.0f, 0.0f}));
}
