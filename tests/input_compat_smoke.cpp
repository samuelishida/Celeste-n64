#include <cassert>
#include "../src/user/gameplay/input/virtual_input.hpp"
using namespace madeline_cube;
int main() {
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
