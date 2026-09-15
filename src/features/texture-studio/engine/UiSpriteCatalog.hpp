#pragma once

#include <string>
#include <string_view>

namespace paimon::texture_studio {

enum class TintScope : int {
    ButtonsOnly     = 0,
    ButtonsAndMenuUi = 1,
    // Legacy: old projects may still store 2. It no longer paints the whole
    // game — shouldTint maps it to ButtonsAndMenuUi, the loader clamps it to
    // 1, and the editor only cycles 0..1. Kept so saved ints keep parsing.
    Everything      = 2,
};

enum class SpriteKind : int {
    Button = 0,
    MenuUi,
    Gameplay,
    Other,
};

class UiSpriteCatalog final {
public:
    static SpriteKind classify(std::string_view frameName,
                               std::string_view sheetBaseName);

    static bool isUiSheet(std::string_view sheetBaseName);

    static bool isGameplaySheet(std::string_view sheetBaseName);

    static bool shouldTint(SpriteKind kind, TintScope scope);

    static char const* kindLabel(SpriteKind kind);

private:
    UiSpriteCatalog() = delete;
};

}  // namespace paimon::texture_studio
