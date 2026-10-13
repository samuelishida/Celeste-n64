#include <cassert>
#include "../src/user/gameplay/input/input_system.hpp"
using namespace madeline_cube;
int main() {
    constexpr float dt = 1.0f / 60.0f;
    PolledButtonState climb;
    VirtualButtonState held;
    climb.Poll(true); // zero simulation ticks: edge remains pending
    climb.Poll(true);
    climb.BeginTick(held, 0.0f, dt);
    assert(held.down && held.pressed && !held.released);
    climb.BeginTick(held, 0.0f, dt); // catch-up: held survives, edge does not
    assert(held.down && !held.pressed);
    climb.Poll(false);
    climb.BeginTick(held, 0.0f, dt);
    assert(!held.down && held.released); // immediate release, no filter
    climb.BeginTick(held, 0.0f, dt);
    assert(!held.released);
    climb.Poll(true);
    climb.Poll(false); // both edges observed before the next tick
    climb.BeginTick(held, 0.0f, dt);
    assert(!held.down && held.pressed && held.released);
    climb.BeginTick(held, 0.0f, dt);
    assert(!held.down && !held.pressed && !held.released);

    PolledButtonState latched;
    VirtualButtonState buffered;
    latched.Poll(true);
    latched.Poll(false);
    for (int i = 0; i < 20; ++i) latched.Poll(false); // zero ticks do not age buffer
    latched.BeginTick(buffered, 0.10f, dt);
    assert(buffered.pressed && buffered.released && !buffered.down);
    assert(buffered.pressed_edge);
    latched.BeginTick(buffered, 0.10f, dt);
    assert(buffered.pressed && !buffered.pressed_edge && !buffered.released);
    assert(ConsumePress(buffered));
    latched.BeginTick(buffered, 0.10f, dt);
    assert(!buffered.pressed && !buffered.released);

    InputSystem adapter;
    adapter.ObserveGameplayButtons(true, true, true);
    adapter.ObserveGameplayButtons(false, false, false); // disconnect before tick
    adapter.BeginTick(dt);
    assert(adapter.Jump().pressed_edge && adapter.Jump().released);
    assert(!adapter.Climb().down && adapter.Climb().released);
    adapter.EndTick(true, true);
    adapter.BeginTick(dt);
    assert(!adapter.Jump().pressed && !adapter.Dash().pressed);
    assert(!adapter.Climb().pressed && !adapter.Climb().released);

    VirtualButtonState jump;
    UpdateVirtualButton(jump, true, 0.10f, 1.0f / 60.0f);
    assert(jump.pressed);
    UpdateVirtualButton(jump, false, 0.10f, 1.0f / 60.0f);
    assert(jump.pressed);
    assert(ConsumePress(jump));
    assert(!ConsumePress(jump));

    VirtualStickState dead = ApplyCircularDeadzone({0.2f, 0.1f}, 0.35f);
    assert(dead.value.x == 0.0f && dead.value.y == 0.0f);
    VirtualStickState live = ApplyCircularDeadzone({0.4f, 0.0f}, 0.35f);
    assert(live.value.x == 0.4f);

    VirtualAxisState axis;
    assert(ResolveTakeNewerAxis(1.0f, 0.0f, axis) < 0.0f);
    assert(ResolveTakeNewerAxis(1.0f, 1.0f, axis) < 0.0f);
    assert(ResolveTakeNewerAxis(0.0f, 1.0f, axis) > 0.0f);
    assert(ResolveTakeNewerAxis(1.0f, 1.0f, axis) > 0.0f);

    // Unbuffered button (climb/pause, buffer_seconds == 0): the pressed edge
    // is a single-frame event, not sticky (Inc 1 fix).
    VirtualButtonState pause;
    UpdateVirtualButton(pause, true, 0.0f, 1.0f / 60.0f);
    assert(pause.pressed);
    UpdateVirtualButton(pause, true, 0.0f, 1.0f / 60.0f);
    assert(!pause.pressed);

    // Buffered edge expires after the 0.10 s window without a consume.
    VirtualButtonState dash;
    UpdateVirtualButton(dash, true, 0.10f, 1.0f / 60.0f);
    assert(dash.pressed);
    for (int i = 0; i < 7; ++i) {
        UpdateVirtualButton(dash, true, 0.10f, 1.0f / 60.0f);
    }
    assert(!dash.pressed);
    return 0;
}
