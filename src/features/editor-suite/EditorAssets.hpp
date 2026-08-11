#pragma once

// Paimon Editor Assets — custom textures with vanilla fallbacks
//
// Assets live in `resources/` and match the names in `files::`. Geode packages
// them in the mod namespace. Loaders fall through to known-good Geometry Dash
// frames if an optional texture is unavailable.
//
// Load order for each icon:
//   1. mod resource  expandSpriteName("paim_....png")  via SpriteHelper::safeCreate
//   2. each fallback as sprite-frame name (GD atlas)
//   3. last-resort frame / file (info icon or square)
#include <Geode/Geode.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/ui/BasedButtonSprite.hpp>
#include <functional>
#include <initializer_list>

namespace paimon::editor::assets {

namespace files {
inline constexpr char const* collab = "paim_collab.png";
} // namespace files

// True if the mod ships a usable custom PNG for this basename.
bool hasCustom(char const* preferredPaim);

// Clickable circle button (lambda). Prefers the mod PNG, then the GD frames
// listed in `fallbacks`.
CCMenuItemSpriteExtra* circleButton(
    char const* preferredPaim,
    std::initializer_list<char const*> fallbacks,
    float topScale,
    geode::CircleBaseColor color,
    std::function<void()> onClick,
    geode::CircleBaseSize size = geode::CircleBaseSize::Tiny
);

} // namespace paimon::editor::assets
