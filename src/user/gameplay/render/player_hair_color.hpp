#pragma once

namespace madeline_cube {

enum class PlayerHairColorState {
    Normal,
    NoDash,
    TwoDashes,
    RefillFlash,
};

// OG Player.Update prioritizes its .05 s refill flash over dash-count color.
// Charge-count colors now follow OG for zero, one, and multiple dashes.
// Feather remains a separate, unmodeled state.
inline PlayerHairColorState ResolvePlayerHairColor(int dashes,
                                                    float refill_flash_remaining) {
    if (refill_flash_remaining > 0.0f) return PlayerHairColorState::RefillFlash;
    if (dashes == 1) return PlayerHairColorState::Normal;
    if (dashes == 0) return PlayerHairColorState::NoDash;
    return PlayerHairColorState::TwoDashes;
}

}  // namespace madeline_cube
