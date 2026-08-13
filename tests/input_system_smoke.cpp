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

static void TestMoveSampling() {
    // SampleStickAxis returns RAW per-axis normalization (no deadzone, no unit
    // magnitude clamp). Contract: {-clamp(x,max)/max, clamp(y,max)/max}.
    // Full deflection on the N64 stick is ±85 hardware, clamped to ±80 by the
    // helper before normalizing, so it maps to unit magnitude per axis.

    // Cardinal right: stick_x=+85 -> -1 (stick right -> +X move).
    Vec2 right = SampleStickAxis(85, 0, 80);
    assert(std::fabs(right.x - (-1.0f)) < 0.001f);
    assert(right.y == 0.0f);

    // Cardinal up: stick_y=+85 -> +1 (stick up -> +Y move).
    Vec2 up = SampleStickAxis(0, 85, 80);
    assert(up.x == 0.0f);
    assert(std::fabs(up.y - 1.0f) < 0.001f);

    // Rest: no deadzone, so 0 input -> 0 output.
    Vec2 rest = SampleStickAxis(0, 0, 80);
    assert(rest.x == 0.0f && rest.y == 0.0f);

    // Small deflection (5/80 = 0.0625): no deadzone, so it is nonzero.
    Vec2 small = SampleStickAxis(5, 0, 80);
    assert(std::fabs(small.x - (-0.0625f)) < 0.001f);

    // Full diagonal: raw per-axis output is {-1, +1} (magnitude ~1.414 before
    // the unit clamp) — do NOT assert 1.0 here.
    Vec2 diag = SampleStickAxis(85, 85, 80);
    assert(std::fabs(diag.x - (-1.0f)) < 0.001f);
    assert(std::fabs(diag.y - 1.0f) < 0.001f);

    // Feed the helper output through the real downstream clamp
    // (InputActionVec2::Update with no deadzone) and assert the FINAL resolved
    // magnitude clamps to 1.0 on a full diagonal. This mirrors the real
    // SampleMoveAxis flow (-85/85 -> {-1,+1} -> Update clamps to 1.0) and
    // guards against reverting kStickMax to 90 or re-adding a deadzone.
    InputActionVec2 act;
    act.Update(diag.x, diag.y, 0.0f);
    const float mag = std::sqrt(
        act.Value().x * act.Value().x + act.Value().y * act.Value().y);
    assert(std::fabs(mag - 1.0f) < 0.001f);

    // Sanity: if the scaling were still 90 (the regressed cap), a full
    // deflection would not reach unit magnitude.
    Vec2 cap90 = SampleStickAxis(85, 0, 90);
    assert(std::fabs(cap90.x - (-0.944f)) < 0.001f);

    std::printf("[input] TestMoveSampling PASS\n");
}

int main() {
    TestButtonTransitions();
    TestAnalogDeadzone();
    TestDigitalCardinal();
    TestPlayerInputConsumption();
    TestMoveSampling();
    std::printf("[input] ALL PASS\n");
    return 0;
}
