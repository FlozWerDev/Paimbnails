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
        // Object outlines/glow (.bro: GameObject::addGlow). NOTE: "block"
        // contains "lock", so this must stay ahead of the MenuUi lock token.
        "blockoutline",
    });
}

bool isColorMeaningfulFrame(std::string const& lower) {
    return containsAny(lower, {
        "difficulty_",
        "difficon_",
        // Red demon face: difficulty identity, not chrome.
        "demonicon",
        // Vault-guardian faces (GJ_secretLock*, incl. SecretSheet variants):
        // character art, not the neutral secretLock01/02/03 padlocks (which
        // stay tintable via the "lock" furniture token below).
        "gj_secretlock",
        // Buttons with reward art baked into the frame: tinting them would
        // recolor the shards/gems/faces they advertise.
        "shardsbtn",
        "normalbtn",
        "videoreward",
        // Baked-content menu buttons (verified pixel by pixel against the
        // official sheets): difficulty faces, rate stars, player cube,
        // checkpoint/practice gems, trophies, chests, coins, crowns, demon
        // crowns, brand logos, map-pack folders, path shards, podiums,
        // skull level nodes. The chrome and the content share one frame,
        // so the whole frame stays vanilla. NOTE: bare "ngbtn" would also
        // match longBtn/songBtn/swingBtn_off, and bare "eventbtn" would
        // match editor buttons, hence the anchored forms.
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
    // Screen furniture: titles, tables, bars, comment boxes, locks, doors,
    // corners, page dots, celebration headers. Neutral chrome, tintable.
    // NOTE: "sideart" was removed: its only frame (GJ_sideArt_001) is colored
    // block-stair decoration, not neutral chrome (verified pixel by pixel).
    // NOTE: there is deliberately no bare "icon" token. It also matched
    // currency, badges, brand logos, chests, shards and reward art
    // (verified pixel by pixel against the official sheets), so each
    // tintable icon family is allowlisted by name below instead. Content
    // art (chests, ropes, crowns, stars/moons/diamonds, shards, coins,
    // secret coins, big reward icons) falls through to Other untouched.
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
        // Standalone options-menu chrome. Verified: no sheet frame matches
        // these; ground truth is the .bro (Slider::create, loading UI,
        // browser page dots) plus the white neutral progress fill.
        // NOTE: GJ_square07 is deliberately NOT here: the game recolors it
        // at runtime (CustomSongWidget::addExtraVisuals setColor yellow),
        // so a pre-tinted pack would double-tint. Same for the GJ_square01
        // solid fills (no menu-only usage evidence; generic filler).
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
            // Legacy value: old projects may still store it. It no longer
            // paints the whole game — map to ButtonsAndMenuUi so at most
            // menu/button UI is tinted. The loader clamps stored 2 to 1 and
            // the editor only cycles 0..1.
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
