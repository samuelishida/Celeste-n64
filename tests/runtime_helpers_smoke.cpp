#include <cassert>
#include <cmath>
#include "gameplay/runtime/math.hpp"
#include "gameplay/runtime/state_machine.hpp"
#include "gameplay/runtime/timing.hpp"
using namespace madeline_cube;
int main() {
    assert(Approach(0.0f, 2.0f, 3.0f) == 2.0f);
    assert(Approach(2.0f, 0.0f, 0.5f) == 1.5f);
    assert(std::fabs(AngleApproach(3.0f, -3.0f, 0.1f) - 3.1f) < 0.001f);
    Vec3 moved = ApproachXZ({0, 7, 0}, {3, 9, 4}, 2.5f);
    assert(std::fabs(moved.x - 1.5f) < 0.001f && moved.y == 7.0f);
    assert(OnInterval(0.51f, 0.02f, 0.5f));
    assert(!OnInterval(0.49f, 0.02f, 0.5f));
    StateMachine machine(2);
    machine.Step(0.25f);
    assert(machine.TimeInState() == 0.25f);
    machine.SetState(4);
    assert(machine.State() == 4 && machine.Previous() == 2 && machine.ChangedThisFrame());
    return 0;
}
