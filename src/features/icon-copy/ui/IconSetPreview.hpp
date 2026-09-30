#pragma once
// shared preview widgets for a copied icon set.

#include <Geode/Enums.hpp>

#include <span>

namespace cocos2d { class CCNode; }

namespace paimon::iconcopy {

struct IconSet;

// simpleplayer wearing the set, centred inside a box-sized node.
cocos2d::CCNode* makePreview(IconSet const& set, IconType type, float box);

// every gamemode a copied set carries, in garage tab order.
std::span<IconType const> previewGamemodes();

// display name for one of those gamemodes.
char const* gamemodeName(IconType type);

}  // namespace paimon::iconcopy
