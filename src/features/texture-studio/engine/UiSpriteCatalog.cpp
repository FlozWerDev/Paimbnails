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
}

bool isGameplayEffectFrame(std::string const& lower) {
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
        // object outlines/glow (.bro: gameobject::addglow). "block" contains "lock": keep ahead of the menuui lock token.
        "blockoutline",
    });
}

bool isColorMeaningfulFrame(std::string const& lower) {
    return containsAny(lower, {
        "difficulty_",
        "difficon_",
        // store icons depict purchasable content rather than neutral UI chrome.
        "storeitemicon",
        // red demon face: difficulty identity, not chrome.
        "demonicon",
        // vault-guardian faces: character art, not the neutral secretlock padlocks (tintable via "lock" below).
        "gj_secretlock",
        // reward art baked in: tinting would recolor the advertised shards/gems/faces.
        "shardsbtn",
        "videoreward",
        "gj_ngbtn",
        "gpbtn",
        "gpgbtn",
        "ncs",
        "everyplay",
        "modbadge",
        "rankicon_",
        "featuredcoin",
        // the editor's color-channel button shows the channel color itself.
        "gj_colorbtn",
        // brand play button carries the YouTube red.
        "youtubeplaybutton",
    });
}

bool isCuratedButtonFrame(std::string const& lower) {
    static constexpr std::array<char const*, 15> kExact = {
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
        "navarrowbtn_001.png",
        "pbtn_arrow_001.png",
        "pbtn_jump_001.png",
        "pbtn_move_001.png",
        "btn_chathistory_001.png",
    };
    for (auto const* n : kExact) {
        if (lower == n) return true;
    }
    if (lower.find("_tab_on") != std::string::npos) return true;
    if (lower.find("_tab_off") != std::string::npos) return true;
    if (lower.find("controllerbtn_") != std::string::npos) return true;
    if (lower.find("accountbtn_") != std::string::npos) return true;
    if (lower.find("warpbtn") != std::string::npos) return true;
    if (lower.find("warplock") != std::string::npos) return true;
    return false;
}

// content art living inside UI sheets: identity/reward colors must stay
// vanilla even though the sheet itself is tintable chrome. Checked after
// the button/menu tokens so e.g. fireShardLabel (a text label) still tints.
bool isContentArtFrame(std::string const& lower) {
    return containsAny(lower, {
        "currency", "coin", "diamond",
        "starsicon", "sstarsicon", "moonsicon", "pointsicon", "bigstar", "bigkey",
        "shard", "tier1icon", "tier2icon", "tier3icon",
        "chest", "shoprope", "dailycrown", "pathicon",
        "discordicon", "yticon", "featuredicon", "likesicon", "completesicon",
        "tiktokicon", "twitchicon", "instaicon", "robtoplogo",
        "newmusicicon", "hammericon", "smallmodeicon", "collaborationicon",
        "gj_square07", "shineburst", "sideart",
    });
}

bool isMenuUiFrame(std::string const& lower) {
    // neutral chrome, tintable. no "sideart" (decoration) and no bare "icon" (reward art); tintable icons allowlisted below.
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
        // difficulty-filter selection outline (white chrome).
        "difficultyselected",
        // options chrome and container panels
        "slider",
        "groove",
        "loadingcircle",
        "smalldot",
        "progressbar",
        "edit_barbg",
        "controller_outline",
        "square02",
        "square04",
        "panel",
        "box",
        // furniture icons: menu controls and containers.
        "foldericon",
        "deleteicon", "deleteallicon",
        "filtericon",
        "infoicon",
        "sorticon",
        "slikeicon", "srecenticon", "sdownloadicon", "sfollowedicon",
        "sfriendsicon", "smagicicon", "smodicon", "strendingicon",
        "gj_musicicon",  // (newmusicicon "new" badges stay vanilla)
        "noteicon",
        "timeicon",
        "extendedicon",
        // neutral browser/editor chrome missed by the icon allowlist above.
        "deletefilter_",
        "edit_vline",
        "hearton", "heartoff",
    });
}

} // namespace

bool UiSpriteCatalog::isUiSheet(std::string_view sheetBaseName) {
    return sheetBaseName == "GJ_GameSheet03"
        || sheetBaseName == "GJ_GameSheet04"
        || sheetBaseName == "GJ_GameSheetEditor"
        || sheetBaseName == "GJ_LaunchSheet"
        || sheetBaseName == "GJ_LaunchSheet2"
        || sheetBaseName == "GJ_ShopSheet"
        || sheetBaseName == "GauntletSheet"
        || sheetBaseName == "TowerSheet"
        || sheetBaseName == "SecretSheet"
        || sheetBaseName == "WorldSheet"
        || sheetBaseName == "TreasureRoomSheet"
        || sheetBaseName == "DungeonSheet";
}

bool UiSpriteCatalog::isGameplaySheet(std::string_view sheetBaseName) {
    if (isUiSheet(sheetBaseName)) return false;
    if (sheetBaseName == "GJ_GameSheet" || sheetBaseName == "GJ_GameSheet02") return true;
    if (sheetBaseName.rfind("GJ_GameSheetIcons", 0) == 0) return true;
    if (sheetBaseName.rfind("GJ_GameSheetGlow", 0) == 0) return true;
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

    if (isContentArtFrame(lower)) return SpriteKind::Other;

    // inside a UI sheet, anything not excluded above is menu chrome: paint
    // it too instead of leaving random vanilla-colored UI pieces behind.
    if (isUiSheet(sheetBaseName)) return SpriteKind::MenuUi;

    return SpriteKind::Other;
}

bool UiSpriteCatalog::shouldTint(SpriteKind kind, TintScope scope) {
    switch (scope) {
        case TintScope::Everything:
            // legacy: maps to buttonsandmenuui (loader clamps 2 to 1).
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
