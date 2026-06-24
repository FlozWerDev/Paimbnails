#include "UiSpriteCatalog.hpp"

#include <algorithm>
#include <array>
#include <cctype>

namespace paimon::texture_studio {

namespace {

std::string toLower(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        out.push_back(static_cast<char>(
            std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

bool containsAny(std::string const& haystack,
                 std::initializer_list<char const*> tokens) {
    for (auto const* t : tokens) {
        if (haystack.find(t) != std::string::npos) return true;
    }
    return false;
}bool isGameplayEffectFrame(std::string const& lower) {
    return containsAny(lower, {
        "portalshine",      // brillo de portales (Sheet04)
        "playerdash",       // efecto dash del jugador (Sheet04)
        "spiderdash",       // efecto dash de spider (Sheet04)
        "boost_",           // boost_01_shine... (Sheet04)
        "player_special",   // previews de efectos del jugador (Sheet03)
        "explosionicon",    // previews de death effects (Sheet03)
        "shipfireicon",     // previews de fuego de nave (Sheet03)
        "gjitem_",          // items 3D del shop (Sheet03)
        "chompo_",          // criatura del shop (Sheet03)
    });
}

bool isColorMeaningfulFrame(std::string const& lower) {
    return containsAny(lower, {
        "difficulty_",      // caritas de dificultad (easy=azul, demon=rojo...)
        "difficon_",        "modbadge",         // badges de moderador
        "rankicon_",        "featuredcoin",
    });
}

bool isCuratedButtonFrame(std::string const& lower) {
    static constexpr std::array<char const*, 10> kExact = {
        "gj_arrow_01_001.png",
        "gj_arrow_02_001.png",
        "gj_arrow_03_001.png",
        "backarrowplain_01_001.png",
        "gj_checkon_001.png",
        "gj_checkoff_001.png",
        "gj_tabon_001.png",
        "gj_taboff_001.png",
        "gj_chrsel_001.png",
        "gj_select_001.png",
    };
    for (auto const* n : kExact) {
        if (lower == n) return true;
    }
    if (lower.find("_tab_on") != std::string::npos) return true;
    if (lower.find("_tab_off") != std::string::npos) return true;
    return false;
}

bool isMenuUiFrame(std::string const& lower) {
    return containsAny(lower, {
        "icon",        // GJ_starsIcon, folderIcon, chestIcon, social icons...
        "txt",         // GJ_optionsTxt, GJ_followTxt...
        "label",       // weeklyLevelLabel, rewardsLabel, shard labels...
        "table_",      // GJ_table_top/side/corner/bottom
        "topbar",      // GJ_topBar
        "sideart",     // GJ_sideArt
        "comment",     // GJ_commentSide/Top (marcos de comentarios)
        "lock",        // GJ_lock, GJ_secretLock, GJLargeLock...
        "rope",        // shopRope, garageRope, adRope
        "corner",      // dailyLevelCorner, gauntletCorner, rewardCorner
        "uidot",
        "crown",       // gj_dailyCrown, gj_weeklyCrown
        "bigstar", "bigmoon", "bigdiamond", "bigkey",  // moneda/logros grandes
        "star_small", "moon_small", "diamond_small", "usercoin_small",
        "shard",       // displays de shards (fire/ice/lava/poison/shadow...)
        "secretcoin",  // secretCoinUI
        "chest",       // chestIcon ya cae en icon; cofres de recompensa
        "levelcomplete", "practicecomplete", "newbest",
        "checkpoint",
    });
}

} // namespace

bool UiSpriteCatalog::isUiSheet(std::string_view sheetBaseName) {
    return sheetBaseName == "GJ_GameSheet03"
        || sheetBaseName == "GJ_GameSheet04";
}

bool UiSpriteCatalog::isGameplaySheet(std::string_view sheetBaseName) {
    if (isUiSheet(sheetBaseName)) return false;
    if (sheetBaseName.rfind("GJ_GameSheet", 0) == 0) return true;
    if (sheetBaseName.rfind("FireSheet", 0) == 0) return true;
    if (sheetBaseName.rfind("PixelSheet", 0) == 0) return true;
    return false;
}

SpriteKind UiSpriteCatalog::classify(std::string_view frameName,
                                     std::string_view sheetBaseName) {
    if (isGameplaySheet(sheetBaseName)) return SpriteKind::Gameplay;

    auto lower = toLower(frameName);

    if (isGameplayEffectFrame(lower)) return SpriteKind::Gameplay;

    if (isColorMeaningfulFrame(lower)) return SpriteKind::Other;

    // Capa 3: botones (token o lista curada).
    if (lower.find("btn") != std::string::npos ||
        lower.find("button") != std::string::npos ||
        isCuratedButtonFrame(lower)) {
        return SpriteKind::Button;
    }

    // Capa 4: chrome de menú.
    if (isMenuUiFrame(lower)) return SpriteKind::MenuUi;

    return SpriteKind::Other;
}

bool UiSpriteCatalog::shouldTint(SpriteKind kind, TintScope scope) {
    switch (scope) {
        case TintScope::Everything:
            return true;
        case TintScope::ButtonsAndMenuUi:
            return kind == SpriteKind::Button || kind == SpriteKind::MenuUi;
        case TintScope::ButtonsOnly:
        default:
            return kind == SpriteKind::Button;
    }
}

char const* UiSpriteCatalog::kindLabel(SpriteKind kind) {
    switch (kind) {
        case SpriteKind::Button:   return "Button";
        case SpriteKind::MenuUi:   return "Menu UI";
        case SpriteKind::Gameplay: return "Gameplay";
        case SpriteKind::Other:    return "Other";
    }
    return "Other";
}

}  // namespace paimon::texture_studio
