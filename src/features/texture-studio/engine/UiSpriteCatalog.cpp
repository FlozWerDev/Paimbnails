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
        "portalshine",
        "playerdash",
        "spiderdash",
        "boost_",
        "player_special",
        "explosionicon",
        "shipfireicon",
        "gjitem_",
        "chompo_",
        // Object outlines/glow (.bro: GameObject::addGlow). "block" contains "lock": keep ahead of the MenuUi lock token.
        "blockoutline",
    });
}

bool isColorMeaningfulFrame(std::string const& lower) {
    return containsAny(lower, {
        "difficulty_",
        "difficon_",
        // Red demon face: difficulty identity, not chrome.
        "demonicon",
        // Vault-guardian faces: character art, not the neutral secretLock padlocks (tintable via "lock" below).
        "gj_secretlock",
        // Reward art baked in: tinting would recolor the advertised shards/gems/faces.
        "shardsbtn",
        "normalbtn",
        "videoreward",
        // Baked-content buttons: chrome and content share one frame, stays vanilla; anchored tokens skip editor buttons.
        "ratediff",
        "starbtn",
        "garagebtn",
        "checkpointbtn",
        "practicebtn",
        "leaderboardbtn",
        "achbtn",
        "dailybtn",
        "weeklybtn",
        "gj_eventbtn",
        "featuredbtn",
        "mappacksbtn",
        "pathsbtn",
        "highscorebtn",
        "worldlevelbtn",
        "adchest",
        "freechest",
        "dailyreward",
        "freestuff",
        "rewardbtn",
        "advideobtn",
        "gj_ngbtn",
        "gpbtn",
        "gpgbtn",
        "ncs",
        "everyplay",
        "modbadge",
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
    // Neutral chrome, tintable. No "sideart" (decoration) and no bare "icon" (reward art); tintable icons allowlisted below.
    return containsAny(lower, {
        "txt",
        "label",
        "table_",
        "topbar",
        "comment",
        "lock",
        "door",
        "corner",
        "uidot",
        "levelcomplete", "practicecomplete", "newbest",
        "checkpoint",
        // Difficulty-filter selection outline (white chrome).
        "difficultyselected",
        // Options chrome. No GJ_square07/square01: the game recolors them at runtime (double-tint).
        "slider",
        "loadingcircle",
        "smalldot",
        "progressbar",
        // Furniture icons: menu controls and containers.
        "foldericon",
        "deleteicon", "deleteallicon",
        "filtericon",
        "infoicon",
        "sorticon",
        "slikeicon", "srecenticon", "sdownloadicon", "sfollowedicon",
        "sfriendsicon", "smagicicon", "smodicon", "strendingicon",
        "gj_musicicon",  // (newMusicIcon "NEW" badges stay vanilla)
        "noteicon",
        "timeicon",
        "extendedicon",
        // Neutral browser/editor chrome missed by the icon allowlist above.
        "deletefilter_",
        "edit_vline",
        "hearton", "heartoff",
        "storeitemicon",
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

    if (lower.find("btn") != std::string::npos ||
        lower.find("button") != std::string::npos ||
        isCuratedButtonFrame(lower)) {
        return SpriteKind::Button;
    }

    if (isMenuUiFrame(lower)) return SpriteKind::MenuUi;

    return SpriteKind::Other;
}

bool UiSpriteCatalog::shouldTint(SpriteKind kind, TintScope scope) {
    switch (scope) {
        case TintScope::Everything:
            // Legacy: maps to ButtonsAndMenuUi (loader clamps 2 to 1).
            return kind == SpriteKind::Button || kind == SpriteKind::MenuUi;
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
