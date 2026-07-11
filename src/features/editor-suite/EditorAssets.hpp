#pragma once

// =============================================================================
// Paimon Editor Assets — custom textures with vanilla fallbacks
//
// Drop PNGs into `resources/` matching the names in `files::`. Until they exist,
// every loader falls through known-good Geometry Dash frames so the editor
// never ships a missing/magenta button.
//
// Load order for each icon:
//   1. mod resource  expandSpriteName("paim_....png")  via SpriteHelper::safeCreate
//   2. each fallback as sprite-frame name (GD atlas)
//   3. last-resort frame / file (info icon or square)
// =============================================================================

#include <Geode/Geode.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/CCMenuItemToggler.hpp>
#include <Geode/ui/BasedButtonSprite.hpp>
#include <functional>
#include <initializer_list>

namespace paimon::editor::assets {

// --- Canonical resource basenames (place these under resources/) -------------
namespace files {
// HUD / undo-menu
inline constexpr char const* hideUiOn        = "paim_hide-ui-on.png";
inline constexpr char const* hideUiOff       = "paim_hide-ui-off.png";
inline constexpr char const* autoBuildHelper = "paim_auto-build-helper.png";
inline constexpr char const* quickExtras     = "paim_edit-extras.png";
inline constexpr char const* backToContent   = "paim_back-to-content.png";
inline constexpr char const* quickSave       = "paim_quick-save.png";
inline constexpr char const* refImage        = "paim_ref-image.png";
inline constexpr char const* refClear        = "paim_ref-clear.png";

// Camera / tools
inline constexpr char const* camToObject     = "paim_cam-to-object.png";
inline constexpr char const* objectToCam     = "paim_object-to-cam.png";
inline constexpr char const* toolBuildHelper = "paim_tool-bh.png";
inline constexpr char const* toolAlignX      = "paim_tool-align-x.png";
inline constexpr char const* toolAlignY      = "paim_tool-align-y.png";
inline constexpr char const* toolLoop        = "paim_tool-loop.png";

// Start pos / search / tabs
inline constexpr char const* startPosPlay    = "paim_startpos-play.png";
inline constexpr char const* startPosPrev    = "paim_startpos-prev.png";
inline constexpr char const* startPosNext    = "paim_startpos-next.png";
inline constexpr char const* objectSearch    = "paim_object-search.png";
inline constexpr char const* favorites       = "paim_favorites.png";
inline constexpr char const* viewTab         = "paim_view-tab.png";
inline constexpr char const* gridIcon        = "paim_grid-icon.png";

// Pause / entry
inline constexpr char const* groupSummary    = "paim_group-summary.png";
inline constexpr char const* objectSummary   = "paim_object-summary.png";
inline constexpr char const* backups         = "paim_backups.png";
inline constexpr char const* collab          = "paim_collab.png";
inline constexpr char const* editorHistory   = "paim_editor-history.png";
} // namespace files

// True if the mod ships a usable custom PNG for this basename.
bool hasCustom(char const* preferredPaim);

// Prefer mod PNG, then GD frames listed in `fallbacks`. Always non-null.
cocos2d::CCSprite* loadIcon(
    char const* preferredPaim,
    std::initializer_list<char const*> fallbacks,
    float scale = 1.f
);

// Icon wrapped in a Geode circle (Tiny by default) — never bare.
geode::CircleButtonSprite* circleIcon(
    char const* preferredPaim,
    std::initializer_list<char const*> fallbacks,
    float topScale,
    geode::CircleBaseColor color,
    geode::CircleBaseSize size = geode::CircleBaseSize::Tiny
);

// Clickable circle button (lambda).
CCMenuItemSpriteExtra* circleButton(
    char const* preferredPaim,
    std::initializer_list<char const*> fallbacks,
    float topScale,
    geode::CircleBaseColor color,
    std::function<void()> onClick,
    geode::CircleBaseSize size = geode::CircleBaseSize::Tiny
);

// Toggler with two circle faces. Arguments are explicitly off, then on, matching
// CCMenuItemToggler::create and avoiding inverted visuals/callback state.
CCMenuItemToggler* circleToggler(
    char const* preferredOff,
    std::initializer_list<char const*> fallbacksOff,
    char const* preferredOn,
    std::initializer_list<char const*> fallbacksOn,
    float topScale,
    geode::CircleBaseColor colorOff,
    geode::CircleBaseColor colorOn,
    cocos2d::CCObject* target,
    cocos2d::SEL_MenuHandler selector,
    geode::CircleBaseSize size = geode::CircleBaseSize::Tiny
);

// Normalize custom items inserted into vanilla 35x40 toolbar cells. This keeps
// mixed circle, sprite, and toggler buttons aligned under RowLayout.
void normalizeToolbarItem(
    CCMenuItemSpriteExtra* item,
    cocos2d::CCSize size = {35.f, 40.f}
);
void normalizeToolbarToggle(
    CCMenuItemToggler* item,
    cocos2d::CCSize size = {35.f, 40.f}
);

// Size a custom item to match an existing sibling cell in a vanilla grid menu
// (editor-buttons-menu) and scale its face to fit, so RowLayout stays uniform.
void fitGridItem(CCMenuItemSpriteExtra* item, cocos2d::CCNode* menu);

// Prefer custom icon circle; if preferred missing and `textFallback` set, use
// a ButtonSprite text label instead (pause menus).
CCMenuItemSpriteExtra* iconOrTextButton(
    char const* preferredPaim,
    std::initializer_list<char const*> fallbacks,
    char const* textFallback,
    char const* textTexture,
    float textScale,
    geode::CircleBaseColor color,
    std::function<void()> onClick
);

// Icon factory for tab side-icons: returns a CCNode (sprite) with custom first.
std::function<cocos2d::CCNode*()> tabIcon(
    char const* preferredPaim,
    char const* fallbackFrame
);

// 40×40 edit-bar style tool (GJ_button_04 plate + icon), matches Tinker
// RelocateBuildTools / EditTools look without custom o_*.png sheets.
CCMenuItemSpriteExtra* editBarToolButton(
    char const* preferredPaim,
    std::initializer_list<char const*> fallbacks,
    std::function<void()> onClick
);

// Vanilla create-tab look: GJ_tabOn/Off with an icon in the middle.
// Used for View/Search/Favs fallback openers when Alpha EditorTab-API owns m_tabsMenu.
CCMenuItemToggler* tabStyleToggler(
    cocos2d::CCSprite* iconOn,
    cocos2d::CCSprite* iconOff,
    std::function<void(bool toggledOn)> onToggle
);

} // namespace paimon::editor::assets
