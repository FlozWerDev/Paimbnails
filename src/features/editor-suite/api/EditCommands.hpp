#pragma once

// Extra EditCommand values for fractional moves (grid-relative).
// Vanilla EditCommand occupies lower values; we use a free high band like BetterEdit.

#include <Geode/Enums.hpp>
#include <cstdint>

namespace paimon::editor {

// Prefer calling moveObject with explicit deltas (EditorHelpers) when possible.
// These constants are for code that must go through moveObjectCall-style APIs.
enum class EditCommandExt : int {
    QuarterLeft  = 0x401,
    QuarterRight = 0x402,
    QuarterUp    = 0x403,
    QuarterDown  = 0x404,
    EighthLeft   = 0x405,
    EighthRight  = 0x406,
    EighthUp     = 0x407,
    EighthDown   = 0x408,
    UnitLeft     = 0x409,
    UnitRight    = 0x40A,
    UnitUp       = 0x40B,
    UnitDown     = 0x40C,
    HalfLeft     = 0x40D,
    HalfRight    = 0x40E,
    HalfUp       = 0x40F,
    HalfDown     = 0x410,
};

inline float fractionStep(float grid, EditCommandExt cmd) {
    switch (cmd) {
        case EditCommandExt::HalfLeft:
        case EditCommandExt::HalfRight:
        case EditCommandExt::HalfUp:
        case EditCommandExt::HalfDown:
            return grid * 0.5f;
        case EditCommandExt::QuarterLeft:
        case EditCommandExt::QuarterRight:
        case EditCommandExt::QuarterUp:
        case EditCommandExt::QuarterDown:
            return grid * 0.25f;
        case EditCommandExt::EighthLeft:
        case EditCommandExt::EighthRight:
        case EditCommandExt::EighthUp:
        case EditCommandExt::EighthDown:
            return grid * 0.125f;
        case EditCommandExt::UnitLeft:
        case EditCommandExt::UnitRight:
        case EditCommandExt::UnitUp:
        case EditCommandExt::UnitDown:
            return 1.f;
    }
    return grid * 0.25f;
}

} // namespace paimon::editor
