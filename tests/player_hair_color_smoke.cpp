#include <cassert>

#include "gameplay/render/player_hair_color.hpp"

using namespace madeline_cube;

int main() {
    assert(ResolvePlayerHairColor(1, 0.0f) == PlayerHairColorState::Normal);
    assert(ResolvePlayerHairColor(0, 0.0f) == PlayerHairColorState::NoDash);
    assert(ResolvePlayerHairColor(2, 0.0f) == PlayerHairColorState::TwoDashes);
    assert(ResolvePlayerHairColor(1, 0.001f) == PlayerHairColorState::RefillFlash);
    assert(ResolvePlayerHairColor(0, 0.001f) == PlayerHairColorState::RefillFlash);
    assert(ResolvePlayerHairColor(2, 0.001f) == PlayerHairColorState::RefillFlash);
}
