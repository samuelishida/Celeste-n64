#include <cassert>
#include <vector>

#include "gameplay/world/actor_world.hpp"

using namespace madeline_cube;

struct Recorder final : Actor {
    Recorder(uint32_t id, int value, std::vector<int>& trace)
        : value_(value), trace_(trace) { source_id = id; }
    void Update(float) override { trace_.push_back(value_); }
    void LateUpdate(float) override { trace_.push_back(value_ + 100); }
    Vec3 UpdateBoundsHalfExtent() const override { return bounds_half_extent_; }
    Vec3 bounds_half_extent_{};
    int value_;
    std::vector<int>& trace_;
};

int main() {
    ActorWorld world;
    std::vector<int> trace;
    Recorder source20(20, 20, trace);
    Recorder source5_first(5, 5, trace);
    Recorder source12(12, 12, trace);
    Recorder source5_second(5, 6, trace);
    Recorder cassette_slot(9, 9, trace);
    assert(world.Add(source20));
    assert(world.Add(source5_first));
    assert(world.Add(source12));
    assert(world.Add(source5_second));
    world.ResolvePending();

    world.BeginSourceOrderedPass(false);
    world.DispatchSourceOrderedBefore(8, 1.0f / 60.0f);
    trace.push_back(8);   // dynamic Solid source slot
    world.DispatchSourceOrderedActorSlot(cassette_slot, 1.0f / 60.0f);
    world.DispatchSourceOrderedBefore(10, 1.0f / 60.0f);
    trace.push_back(10);  // Player source slot
    world.DispatchSourceOrderedBefore(18, 1.0f / 60.0f);
    trace.push_back(18);  // next dynamic Solid source slot
    world.DispatchSourceOrderedRemainder(1.0f / 60.0f);
    assert((trace == std::vector<int>{5, 6, 8, 9, 10, 12, 18, 20}));

    trace.clear();
    world.BeginSourceOrderedPass(true);
    world.DispatchSourceOrderedBefore(8, 1.0f / 60.0f);
    trace.push_back(108);
    world.DispatchSourceOrderedActorSlot(cassette_slot, 1.0f / 60.0f);
    world.DispatchSourceOrderedBefore(10, 1.0f / 60.0f);
    trace.push_back(110);
    world.DispatchSourceOrderedBefore(18, 1.0f / 60.0f);
    trace.push_back(118);
    world.DispatchSourceOrderedRemainder(1.0f / 60.0f);
    assert((trace == std::vector<int>{105, 106, 108, 109, 110, 112, 118, 120}));

    // OG's single pre-pass view is shared by Update and LateUpdate. Offscreen
    // actors skip both phases unless their source UpdateOffScreen flag is set.
    source5_first.position = {0.0f, 0.0f, 2.0f};
    source5_first.bounds_half_extent_ = {0.5f, 0.5f, 0.5f};
    source5_second.position = {0.0f, 0.0f, -20.0f};
    source5_second.bounds_half_extent_ = {1.0f, 1.0f, 1.0f};
    source12.position = {100.0f, 0.0f, 0.0f};
    source12.bounds_half_extent_ = {1.0f, 1.0f, 1.0f};
    source12.update_off_screen = true;
    source20.position = {0.0f, 0.0f, -30.0f};

    ActorUpdateView view{};
    view.min = {-5.0f, -5.0f, 0.0f};
    view.max = {5.0f, 5.0f, 10.0f};
    view.valid = true;
    trace.clear();
    world.BeginSourceOrderedPass(false, &view);
    world.DispatchSourceOrderedRemainder(1.0f / 60.0f);
    assert((trace == std::vector<int>{5, 12}));

    trace.clear();
    world.BeginSourceOrderedPass(true, &view);
    world.DispatchSourceOrderedRemainder(1.0f / 60.0f);
    assert((trace == std::vector<int>{105, 112}));

    // External actors use same frozen visibility gate as registered actors.
    cassette_slot.position = {100.0f, 0.0f, 0.0f};
    trace.clear();
    world.BeginSourceOrderedPass(false, &view);
    world.DispatchSourceOrderedActorSlot(cassette_slot, 1.0f / 60.0f);
    world.DispatchSourceOrderedRemainder(1.0f / 60.0f);
    assert((trace == std::vector<int>{5, 12}));
    cassette_slot.update_off_screen = true;
    trace.clear();
    world.BeginSourceOrderedPass(false, &view);
    world.DispatchSourceOrderedActorSlot(cassette_slot, 1.0f / 60.0f);
    world.DispatchSourceOrderedRemainder(1.0f / 60.0f);
    assert((trace == std::vector<int>{5, 9, 12}));
}
