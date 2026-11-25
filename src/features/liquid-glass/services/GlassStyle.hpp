#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>

namespace paimon::glass {

struct GlassColor {
    std::uint8_t r = 255;
    std::uint8_t g = 255;
    std::uint8_t b = 255;
    bool operator==(GlassColor const&) const = default;
};

struct NamedGlassColor {
    char const* frame;
    GlassColor color;
};

// Line icon a round or square utility button draws over its glass.
// The numeric values are the shader's icon ids (u_shadow.w).
enum class GlassGlyph : std::uint8_t {
    None = 0,
    Close = 1,
    Refresh = 2,
    ChevronLeft = 3,
    ChevronRight = 4,
    Check = 5,
    Info = 6,
    Plus = 7,
    Minus = 8,
    Play = 9,
    Stop = 10,
    Download = 11,
};

// Geometry / rendering mode of a glass plate.
// Auto: pill if wide (>= 1.25x), rounded square otherwise (ButtonSprites).
// Circle: centred circle on a sprite host (round utility buttons, thumbs).
// RoundedSquare: squircle button (checkboxes, plain/cross/account buttons).
// Pill: full capsule (long buttons, tabs, slider grooves, scrollbars).
// SpriteMask: follows the host CCSprite's alpha silhouette and preserves its
// inner icon while turning its body into refracting Liquid Glass.
enum class GlassShape : std::uint8_t {
    Auto = 0,
    Circle = 1,
    RoundedSquare = 2,
    Pill = 3,
    SpriteMask = 4,
};

// Rendering treatment when GlassShape::SpriteMask is active (passed via u_spriteOpts.w):
// IconButton: isolate and preserve the inner baked icon over refracting glass.
// Tab: replace the wood/flat tab fill with glass while keeping baked text crisp.
// Chrome: pure frosted refracting glass fill for list border/header strips.
enum class GlassSpriteStyle : std::uint8_t {
    IconButton = 0,
    Tab = 1,
    Chrome = 2,
};

// GD button skins and the glass tint that keeps their meaning (green =
// confirm, red = cancel...). Lighter than the art so the glass stays clear.
inline constexpr NamedGlassColor kButtonGlassColors[] = {
    {"GJ_button_01.png", {80, 224, 110}},  // green
    {"GJ_button_02.png", {70, 205, 255}},  // cyan
    {"GJ_button_03.png", {255, 110, 210}}, // pink
    {"GJ_button_04.png", {160, 168, 186}}, // gray
    {"GJ_button_05.png", {80, 130, 255}},  // blue
    {"GJ_button_06.png", {255, 92, 92}},   // red
};

inline std::optional<GlassColor> buttonGlassColor(std::string_view frame) {
    for (auto const& entry : kButtonGlassColors) {
        if (frame == entry.frame) return entry.color;
    }
    return std::nullopt;
}

// A neutral gray skin carries no meaning, so it takes the user's tint.
inline bool isNeutralButtonFrame(std::string_view frame) {
    return frame == "GJ_button_04.png";
}

namespace detail {

inline std::string toLowerAscii(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

inline bool containsAny(std::string_view haystack, std::initializer_list<char const*> needles) {
    for (char const* needle : needles) {
        if (haystack.find(needle) != std::string_view::npos) return true;
    }
    return false;
}

// Extract just the basename without directory prefix or -hd/-uhd resolution suffix.
inline std::string normalizedBaseName(std::string_view pathOrName) {
    auto slash = pathOrName.find_last_of("/\\");
    std::string_view file = slash == std::string_view::npos ? pathOrName : pathOrName.substr(slash + 1);
    std::string lower = toLowerAscii(file);
    for (char const* ext : {".png", ".plist", ".webp", ".jpg"}) {
        std::string_view e{ext};
        if (lower.size() > e.size() && lower.compare(lower.size() - e.size(), e.size(), e) == 0) {
            lower.erase(lower.size() - e.size());
            break;
        }
    }
    for (char const* suf : {"-uhd", "-hd"}) {
        std::string_view s{suf};
        if (lower.size() > s.size() && lower.compare(lower.size() - s.size(), s.size(), s) == 0) {
            lower.erase(lower.size() - s.size());
            break;
        }
    }
    return lower;
}

} // namespace detail

// True for UI sprite sheets whose frames are menu/dialog chrome.
inline bool isGlassUiSheet(std::string_view sheetOrPath) {
    std::string const base = detail::normalizedBaseName(sheetOrPath);
    if (base.empty()) return false;
    static constexpr std::array<char const*, 11> kUiSheets = {
        "gj_gamesheet03",
        "gj_gamesheet04",
        "gj_launchsheet",
        "gj_launchsheet2",
        "gj_shopsheet",
        "gauntletsheet",
        "towersheet",
        "secretsheet",
        "worldsheet",
        "treasureroomsheet",
        "dungeonsheet",
    };
    for (char const* name : kUiSheets) {
        if (base == name) return true;
    }
    // Geode loader's own UI sheet (baseCircle_*, GE_square*, etc.).
    std::string const full = detail::toLowerAscii(sheetOrPath);
    if (full.find("geode.loader") != std::string::npos &&
        (base.find("sheet") != std::string::npos || base.find("base") == 0)) {
        return true;
    }
    return false;
}

// True for gameplay/icon/object/background sheets. Glass NEVER touches these.
inline bool isGlassGameplaySheet(std::string_view sheetOrPath) {
    if (isGlassUiSheet(sheetOrPath)) return false;
    std::string const base = detail::normalizedBaseName(sheetOrPath);
    if (base.empty()) return false;
    if (base == "gj_gamesheet" || base == "gj_gamesheet02" || base == "gj_gamesheeteditor") return true;
    if (base.rfind("gj_gamesheeticons", 0) == 0) return true;
    if (base.rfind("gj_gamesheetglow", 0) == 0) return true;
    if (base.rfind("gj_gamesheet", 0) == 0) return true;
    if (base.rfind("firesheet", 0) == 0) return true;
    if (base.rfind("pixelsheet", 0) == 0) return true;
    if (base.rfind("gamebg", 0) == 0 || base.rfind("groundsquare", 0) == 0) return true;
    return false;
}

// Solid GD/Geode UI panel textures eligible for Liquid Glass panels & wells.
// Explicitly excludes GJ_square07.png (a hollow border overlay drawn over
// thumbnails and previews) and any gameplay texture.
inline bool isGlassPanelTexture(std::string_view pathOrName) {
    std::string const base = detail::normalizedBaseName(pathOrName);
    if (base.empty()) return false;
    static constexpr std::array<char const*, 14> kPanels = {
        "gj_square01",
        "gj_square02",
        "gj_square03",
        "gj_square04",
        "gj_square05",
        "gj_square06",
        "square02_001",
        "square02b_001",
        "square02_small",
        "square02b_small",
        "ge_square01",
        "ge_square02",
        "ge_square03",
        "black-square",
    };
    for (char const* name : kPanels) {
        if (base == name) return true;
    }
    return false;
}

inline bool isGlassCornerFrame(std::string_view frameName) {
    std::string const base = detail::normalizedBaseName(frameName);
    return base == "dailylevelcorner_001" || base == "rewardcorner_001" ||
        base == "gauntletcorner_001" || base == "gj_sideart_001";
}

inline bool isGlassListBorderFrame(std::string_view frameName) {
    std::string const base = detail::normalizedBaseName(frameName);
    return base == "gj_commenttop_001" || base == "gj_commenttop2_001" ||
        base == "gj_commentside_001" || base == "gj_commentside2_001" ||
        base == "gj_table_top_001" || base == "gj_table_top02_001" ||
        base == "gj_table_bottom_001" || base == "gj_table_side_001";
}

inline bool isSliderGrooveTexture(std::string_view pathOrName) {
    std::string const base = detail::normalizedBaseName(pathOrName);
    return base == "slidergroove" || base == "slidergroove2" || base == "slidergroove_02";
}

inline bool isSliderThumbTexture(std::string_view pathOrName) {
    std::string const base = detail::normalizedBaseName(pathOrName);
    return base == "sliderthumb" || base == "sliderthumbsel";
}

struct GlassButtonSpec {
    bool match = false;
    GlassShape shape = GlassShape::Auto;
    GlassGlyph glyph = GlassGlyph::None;
    std::optional<GlassColor> color;
    // True for GD arrows that point left by default and mirror via flipX.
    bool mirrorArrow = false;
    GlassSpriteStyle spriteStyle = GlassSpriteStyle::IconButton;
    // True when empty solid glass expects child icon/label nodes on top;
    // if none exist at runtime, GlassDecorator falls back to SpriteMask.
    bool requiresOverlayChild = false;
};

// Map Geode BasedButtonSprite base textures (e.g. "baseCircle_Medium_Green.png")
// to their clean shape and semantic glass tint.
inline std::optional<GlassButtonSpec> classifyGeodeBaseButton(std::string_view frameOrPath) {
    std::string const base = detail::normalizedBaseName(frameOrPath);
    if (base.rfind("base", 0) != 0) return std::nullopt;
    GlassShape shape = GlassShape::RoundedSquare;
    if (base.rfind("basecircle_", 0) == 0) {
        shape = GlassShape::Circle;
    } else if (base.rfind("basetab_", 0) == 0) {
        shape = GlassShape::Pill;
    } else if (base.rfind("basecross_", 0) == 0 ||
               base.rfind("baseaccount_", 0) == 0 ||
               base.rfind("baseiconselect_", 0) == 0 ||
               base.rfind("baseleaderboard_", 0) == 0 ||
               base.rfind("basecategory_", 0) == 0 ||
               base.rfind("baseeditor_", 0) == 0) {
        shape = GlassShape::RoundedSquare;
    } else {
        return std::nullopt;
    }

    std::optional<GlassColor> color;
    if (base.find("_darkpurple") != std::string::npos || base.find("_purple") != std::string::npos) {
        color = GlassColor{155, 105, 255};
    } else if (base.find("_darkaqua") != std::string::npos || base.find("_aqua") != std::string::npos ||
               base.find("_teal") != std::string::npos) {
        color = GlassColor{60, 200, 215};
    } else if (base.find("_cyan") != std::string::npos || base.find("_lightblue") != std::string::npos ||
               base.find("_selected") != std::string::npos) {
        color = GlassColor{70, 205, 255};
    } else if (base.find("_blue") != std::string::npos) {
        color = GlassColor{80, 130, 255};
    } else if (base.find("_green") != std::string::npos) {
        color = GlassColor{80, 224, 110};
    } else if (base.find("_pink") != std::string::npos || base.find("_magenta") != std::string::npos) {
        color = GlassColor{255, 110, 210};
    } else if (base.find("_red") != std::string::npos || base.find("_salmon") != std::string::npos) {
        color = GlassColor{255, 92, 92};
    } else if (base.find("_orange") != std::string::npos) {
        color = GlassColor{255, 165, 72};
    }
    // Gray / DarkGray / Unselected stay neutral (user tint).
    return GlassButtonSpec{true, shape, GlassGlyph::None, color, false, GlassSpriteStyle::IconButton, true};
}

// Pure classifier for any UI button CCSprite (from a UI sheet or standalone UI texture).
// Gameplay assets, player icons, difficulty faces, color swatches, currencies and
// brand logos are strictly excluded.
inline GlassButtonSpec classifyUiButtonFrame(std::string_view frameName, std::string_view sheetOrTexture) {
    if (isGlassGameplaySheet(sheetOrTexture)) return {};
    if (frameName.empty() && sheetOrTexture.empty()) return {};

    std::string const raw = frameName.empty()
        ? detail::normalizedBaseName(sheetOrTexture)
        : detail::normalizedBaseName(frameName);
    if (raw.empty()) return {};

    // Never touch gameplay sprites, player icons, or editor object outlines.
    if (detail::containsAny(raw, {
            "portalshine", "playerdash", "spiderdash", "boost_", "player_special",
            "explosionicon", "shipfireicon", "gjitem_", "chompo_", "blockoutline",
        })) {
        return {};
    }
    for (char const* prefix : {
            "gamebg", "groundsquare", "player_", "ship_", "ball_", "bird_",
            "dart_", "robot_", "spider_", "swing_", "jetpack_", "pbtn_", "controllerbtn_",
        }) {
        if (raw.rfind(prefix, 0) == 0) return {};
    }

    // Never touch content art, difficulty faces, color swatches, currencies or brand logos.
    if (detail::containsAny(raw, {
            "difficulty_", "difficon_", "demonicon", "gj_colorbtn",
            "currency", "coin", "diamond", "starsicon", "sstarsicon",
            "moonsicon", "pointsicon", "bigstar", "bigkey", "tier1icon",
            "tier2icon", "tier3icon", "chest_", "shoprope", "storeitemicon",
            "dailycrown", "pathicon", "modbadge", "rankicon_", "featuredcoin",
            "dialogicon_", "gj_secretlock", "smalldot", "uidot", "shineburst",
            "sideart", "robtoplogo", "youtubeplaybutton", "gj_ngbtn", "gj_gpbtn",
            "gj_gpgbtn", "gj_everyplaybtn", "gj_ncslibrarybtn", "discordicon",
            "yticon", "twicon", "fbicon", "twitchicon", "tiktokicon", "instaicon",
            "newmusicicon", "featuredicon", "likesicon", "completesicon",
            "collaborationicon", "smallmodeicon", "hammericon", "gj_square07",
            "gj_pausebtn",
        })) {
        return {};
    }

    // 1. Standalone GJ_button_01..06 used directly as a CCSprite.
    if (raw.rfind("gj_button_0", 0) == 0 && raw.size() == 12) {
        char const digit = raw[11];
        if (digit >= '1' && digit <= '6') {
            std::string canonical = "GJ_button_0";
            canonical.push_back(digit);
            canonical += ".png";
            auto color = isNeutralButtonFrame(canonical) ? std::nullopt : buttonGlassColor(canonical);
            return {true, GlassShape::Auto, GlassGlyph::None, color, false, GlassSpriteStyle::IconButton, true};
        }
    }

    // 2. Geode BasedButtonSprite bases (baseCircle_*, baseCross_*, baseTab_*, etc.).
    if (auto geodeSpec = classifyGeodeBaseButton(raw)) return *geodeSpec;

    // 3. Slider thumbs.
    if (raw == "sliderthumb") {
        return {true, GlassShape::Circle, GlassGlyph::None, std::nullopt, false, GlassSpriteStyle::IconButton, false};
    }
    if (raw == "sliderthumbsel") {
        return {true, GlassShape::Circle, GlassGlyph::None, GlassColor{70, 205, 255}, false, GlassSpriteStyle::IconButton, false};
    }

    // 4. Pure composite GD button bases (keep their label/icon children crisp on top).
    // Empty solid glass is only used when child content is present (requiresOverlayChild).
    if (raw == "gj_plainbtn_001") {
        return {true, GlassShape::RoundedSquare, GlassGlyph::None, GlassColor{80, 224, 110}, false, GlassSpriteStyle::IconButton, true};
    }
    if (raw == "gj_plainbtn2_001") {
        return {true, GlassShape::RoundedSquare, GlassGlyph::None, GlassColor{70, 205, 255}, false, GlassSpriteStyle::IconButton, true};
    }
    if (raw == "gj_longbtn01_001" || raw == "gj_longbtn03_001") {
        return {true, GlassShape::Pill, GlassGlyph::None, GlassColor{80, 224, 110}, false, GlassSpriteStyle::IconButton, true};
    }
    if (raw == "gj_longbtn02_001") {
        return {true, GlassShape::Pill, GlassGlyph::None, GlassColor{70, 205, 255}, false, GlassSpriteStyle::IconButton, true};
    }
    if (raw == "gj_longbtn04_001") {
        return {true, GlassShape::Pill, GlassGlyph::None, GlassColor{255, 110, 210}, false, GlassSpriteStyle::IconButton, true};
    }
    if (raw == "gj_tabon_001") {
        return {true, GlassShape::Pill, GlassGlyph::None, GlassColor{70, 205, 255}, false, GlassSpriteStyle::Tab, true};
    }
    if (raw == "gj_taboff_001") {
        return {true, GlassShape::Pill, GlassGlyph::None, std::nullopt, false, GlassSpriteStyle::Tab, true};
    }
    if (raw == "gj_checkoff_001") {
        return {true, GlassShape::RoundedSquare, GlassGlyph::None, std::nullopt, false, GlassSpriteStyle::IconButton, false};
    }
    if (raw == "gj_checkon_001") {
        return {true, GlassShape::RoundedSquare, GlassGlyph::Check, GlassColor{80, 224, 110}, false, GlassSpriteStyle::IconButton, false};
    }

    // GJ_longBtn05/06/07 (158x122 UHD) have baked inner search/user/list icons
    // and no children, so they use contour-masked glass to preserve their icons.
    if (raw == "gj_longbtn05_001") {
        return {true, GlassShape::SpriteMask, GlassGlyph::None, std::nullopt, false, GlassSpriteStyle::IconButton, false};
    }
    if (raw == "gj_longbtn06_001") {
        return {true, GlassShape::SpriteMask, GlassGlyph::None, GlassColor{70, 205, 255}, false, GlassSpriteStyle::IconButton, false};
    }
    if (raw == "gj_longbtn07_001") {
        return {true, GlassShape::SpriteMask, GlassGlyph::None, GlassColor{255, 92, 92}, false, GlassSpriteStyle::IconButton, false};
    }

    // 5. Geometric utility GD buttons with crisp SDF vector glyphs.
    if (raw == "gj_closebtn_001" || raw == "gj_deletebtn_001" || raw == "gj_canceldownloadbtn_001") {
        return {true, GlassShape::Circle, GlassGlyph::Close, GlassColor{255, 92, 92}, false};
    }
    if (raw == "gj_updatebtn_001") {
        return {true, GlassShape::Circle, GlassGlyph::Refresh, GlassColor{70, 205, 255}, false};
    }
    if (raw == "gj_arrow_01_001" || raw == "gj_arrow_02_001" || raw == "gj_arrow_03_001" ||
        raw == "backarrowplain_01_001") {
        return {true, GlassShape::Circle, GlassGlyph::ChevronLeft, std::nullopt, true};
    }
    if (raw == "navarrowbtn_001") {
        return {true, GlassShape::SpriteMask, GlassGlyph::None, std::nullopt, false, GlassSpriteStyle::IconButton, false};
    }
    if (raw == "gj_infobtn_001" || raw == "gj_infoicon_001") {
        return {true, GlassShape::Circle, GlassGlyph::Info, GlassColor{70, 205, 255}, false};
    }
    if (raw == "gj_plusbtn_001" || raw == "gj_plus2btn_001" || raw == "gj_plus3btn_001" ||
        raw == "gj_zoominbtn_001") {
        return {true, GlassShape::Circle, GlassGlyph::Plus, GlassColor{80, 224, 110}, false};
    }
    if (raw == "gj_minusbtn_001" || raw == "gj_zoomoutbtn_001") {
        return {true, GlassShape::Circle, GlassGlyph::Minus, GlassColor{255, 92, 92}, false};
    }
    if (raw == "gj_playmusicbtn_001") {
        return {true, GlassShape::Circle, GlassGlyph::Play, GlassColor{80, 224, 110}, false};
    }
    if (raw == "gj_stopmusicbtn_001") {
        return {true, GlassShape::Circle, GlassGlyph::Stop, GlassColor{255, 92, 92}, false};
    }
    if (raw == "gj_downloadbtn_001") {
        return {true, GlassShape::Circle, GlassGlyph::Download, GlassColor{80, 224, 110}, false};
    }

    // 6. All other GD baked UI icon buttons & leaderboard tabs living on UI sheets
    // (MenuLayer, CreatorLayer, LeaderboardsLayer, LevelInfoLayer, LevelBrowserLayer,
    // GJGarageLayer, PauseLayer, EndLevelLayer, ProfilePage, Shop, etc.) get
    // contour-matched Liquid Glass preserving their inner icon or baked tab label.
    bool const inUiSheet = isGlassUiSheet(sheetOrTexture);
    if (inUiSheet) {
        if (raw.find("_tab_on") != std::string::npos) {
            return {true, GlassShape::SpriteMask, GlassGlyph::None, GlassColor{70, 205, 255}, false, GlassSpriteStyle::Tab, false};
        }
        if (raw.find("_tab_off") != std::string::npos) {
            return {true, GlassShape::SpriteMask, GlassGlyph::None, std::nullopt, false, GlassSpriteStyle::Tab, false};
        }
        bool const isButtonToken = raw.find("btn") != std::string::npos ||
            raw.find("button") != std::string::npos ||
            raw == "gj_chrsel_001" || raw == "gj_select_001";
        if (isButtonToken) {
            // Standalone bare shard art (without btn) already excluded; GJ_shardsBtn
            // keeps its shard graphic legible through SpriteMask.
            return {true, GlassShape::SpriteMask, GlassGlyph::None, std::nullopt, false, GlassSpriteStyle::IconButton, false};
        }
    }

    return {};
}

} // namespace paimon::glass
