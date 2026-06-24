#pragma once

#include <string>
#include <string_view>

namespace paimon::texture_studio {

enum class TintScope : int {
    ButtonsOnly     = 0,   // default: solo botones
    ButtonsAndMenuUi = 1,  // botones + chrome de menú
    Everything      = 2,   // legacy estilo PackGen: todo el sheet
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
