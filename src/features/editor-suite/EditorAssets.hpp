#pragma once

#include <Geode/Geode.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/ui/BasedButtonSprite.hpp>
#include <functional>
#include <initializer_list>

namespace paimon::editor::assets {

namespace files {
inline constexpr char const* collab = "paim_collab.png";
} // namespace files

bool hasCustom(char const* preferredPaim);

CCMenuItemSpriteExtra* circleButton(
    char const* preferredPaim,
    std::initializer_list<char const*> fallbacks,
    float topScale,
    geode::CircleBaseColor color,
    std::function<void()> onClick,
    geode::CircleBaseSize size = geode::CircleBaseSize::Tiny
);

// targetSize <= 0 keeps the base's native size.
CCMenuItemSpriteExtra* squareButton(
    char const* preferredPaim,
    std::initializer_list<char const*> fallbacks,
    float topScale,
    geode::EditorBaseColor color,
    std::function<void()> onClick,
    float targetSize = 0.f
);

} // namespace paimon::editor::assets
