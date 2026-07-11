#pragma once

// =============================================================================
// Paimon Editor UI Kit — shared building blocks for every editor-suite widget.
//
// Rules enforced here (so individual modules can't drift):
//   * A button NEVER ships without a texture: frameIcon() always returns a
//     valid sprite (falls back through a chain of known-good frames).
//   * Text toggles/buttons are FIXED-SIZE so mixed label lengths don't
//     produce mixed button sizes inside EditButtonBar grids.
//   * HUD clusters sit on a shared dark pill/panel background so they read
//     as one coherent surface instead of loose floating sprites.
// =============================================================================

#include <Geode/Geode.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/CCMenuItemToggler.hpp>
#include <Geode/ui/BasedButtonSprite.hpp>
#include <functional>
#include <initializer_list>

namespace paimon::editor::uikit {

// --- Sizing constants (shared look) ---
constexpr float kToggleWidth = 54.f;   // fixed width for text toggles in bars
constexpr float kToggleHeight = 24.f;  // fixed height for text toggles
constexpr float kRowHeight = 20.f;     // checkbox row height in panels

// First frame in the list that actually exists; guaranteed non-null.
// Final fallbacks: GJ_infoIcon_001.png, then a plain square file sprite.
cocos2d::CCSprite* frameIcon(std::initializer_list<char const*> frames, float scale = 1.f);

// Fixed-size text toggle (gray off / green on). For EditButtonBar grids and
// side panels. `onChange` receives the NEW state after the click.
CCMenuItemToggler* fixedToggle(char const* label, bool on, std::function<void(bool)> onChange);

// Fixed-size text button (one-shot action).
CCMenuItemSpriteExtra* fixedButton(
    char const* label, char const* texture, std::function<void()> onClick
);

// Small square stepper button (e.g. "-", "+").
CCMenuItemSpriteExtra* fixedSmallButton(
    char const* label, char const* texture, std::function<void()> onClick
);

// Checkbox row "[x] Label" where the WHOLE row is clickable. `width` is the
// row width; label sits right of the box.
CCMenuItemToggler* checkboxRow(
    char const* label, float width, bool on, std::function<void(bool)> onChange
);

// Icon on a small circle base (never a bare floating icon).
CCMenuItemSpriteExtra* circleIconButton(
    std::initializer_list<char const*> frames,
    float iconScale,
    geode::CircleBaseColor color,
    std::function<void()> onClick
);

// Dark rounded panel background (square02b tinted black).
cocos2d::extension::CCScale9Sprite* darkPanel(
    cocos2d::CCSize size, GLubyte opacity = 130
);

// Smaller/flatter pill for compact HUD clusters (grid size, start pos, ...).
cocos2d::extension::CCScale9Sprite* hudPill(
    cocos2d::CCSize size, GLubyte opacity = 105
);

// Small caption label (bigFont, dimmed) used as titles/hints on panels.
cocos2d::CCLabelBMFont* caption(char const* text, float scale = 0.3f);

// Tiny hint label (chatFont, dimmed) for keybind hints etc.
cocos2d::CCLabelBMFont* hint(char const* text, float scale = 0.45f);

} // namespace paimon::editor::uikit
