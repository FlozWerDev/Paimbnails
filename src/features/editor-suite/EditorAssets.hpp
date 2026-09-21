#pragma once

// Custom textures with GD fallbacks.
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

CCMenuItemSpriteExtra* circleButton(
    char const* preferredPaim,
    std::initializer_list<char const*> fallbacks,
    float topScale,
    geode::CircleBaseColor color,
    std::function<void()> onClick,
    geode::CircleBaseSize size = geode::CircleBaseSize::Tiny
);

} // namespace paimon::editor::assets
