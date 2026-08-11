#include <cassert>
#include <cstdio>
#include <cmath>

#include "../src/user/gameplay/input/input_system.hpp"
#include "../src/user/gameplay/input/input_button.hpp"
#include "../src/user/gameplay/player/player_state.hpp"

using namespace madeline_cube;

// Host-side tests for the InputSystem. These validate the pure logic layers
// (InputButton transition semantics, InputActionVec2 deadzone/analog, and
// PlayerInput press-consumption). They do NOT touch libdragon joypad polling.

static void TestButtonTransitions() {
    InputButton btn;

    // initial: not pressed, not held
    assert(!btn.Down() && !btn.Pressed() && !btn.Released());

    // held frame 1 -> Pressed + Down
    btn.Update(true);
    assert(btn.Down() && btn.Pressed() && !btn.Released());

    // Consume the press
    assert(btn.Pressed());
    btn.ConsumePress();
    assert(!btn.Pressed());  // consumed
    assert(btn.Down());      // still held

    // held frame 2 -> no new Pressed, still Down
    btn.Update(true);
    assert(btn.Down() && !btn.Pressed() && !btn.Released());

    // released frame -> Released + !Down
    btn.Update(false);
    assert(!btn.Down() && !btn.Pressed() && btn.Released());

    std::printf("[input] TestButtonTransitions PASS\n");
}

static void TestAnalogDeadzone() {
    InputActionVec2 act;

    // inside deadzone -> zero
    act.Update(0.05f, 0.05f, 0.15f);
    assert(act.Value().x == 0.0f && act.Value().y == 0.0f);

    // full deflection -> magnitude 1.0
    act.Update(1.0f, 0.0f, 0.15f);
    assert(std::fabs(act.Value().x - 1.0f) < 0.001f);
    assert(act.Value().y == 0.0f);

    // diagonal full -> magnitude ~1 (not sqrt(2))
    act.Update(1.0f, 1.0f, 0.15f);
    const float mag = std::sqrt(
        act.Value().x * act.Value().x + act.Value().y * act.Value().y);
    assert(std::fabs(mag - 1.0f) < 0.001f);

    // partial deflection past deadzone -> magnitude < 1
    act.Update(0.5f, 0.0f, 0.15f);
    assert(act.Value().x > 0.3f && act.Value().x < 1.0f);

    std::printf("[input] TestAnalogDeadzone PASS\n");
}

static void TestDigitalCardinal() {
    InputActionVec2 act;

    act.Update(0.0f, 0.0f, 0.15f);
    act.AddDigital(0.0f, -1.0f);  // up
    assert(std::fabs(act.Value().y - (-1.0f)) < 0.001f);
    assert(act.Value().x == 0.0f);

    act.Reset();
    act.AddDigital(1.0f, 0.0f);  // right
    assert(std::fabs(act.Value().x - 1.0f) < 0.001f);

    // right + up -> diagonal, magnitude 1
    act.Reset();
    act.AddDigital(1.0f, 0.0f);
    act.AddDigital(0.0f, -1.0f);
    const float mag = std::sqrt(
        act.Value().x * act.Value().x + act.Value().y * act.Value().y);
    assert(std::fabs(mag - 1.0f) < 0.001f);

    std::printf("[input] TestDigitalCardinal PASS\n");
}

static void TestPlayerInputConsumption() {
    // A single press must be claimable by exactly one action.
    PlayerInput in;
    in.jump_pressed = true;
    in.dash_pressed = true;

    assert(in.ConsumeJumpPress());
    assert(!in.ConsumeJumpPress());  // already claimed
    assert(in.ConsumeDashPress());
    assert(!in.ConsumeDashPress());

    // After a claim, a second action cannot see the edge.
    PlayerInput in2;
    in2.jump_pressed = true;
    assert(in2.ConsumeJumpPress());
    assert(!in2.ConsumeJumpPress());

    std::printf("[input] TestPlayerInputConsumption PASS\n");
}

int main() {
    TestButtonTransitions();
    TestAnalogDeadzone();
    TestDigitalCardinal();
    TestPlayerInputConsumption();
    std::printf("[input] ALL PASS\n");
    return 0;
}
