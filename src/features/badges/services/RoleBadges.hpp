#pragma once

#include <Geode/Geode.hpp>
#include "RoleService.hpp"
#include <string>

namespace paimon::badges {

// Mod-prefixed node IDs; "mod" keeps the historical paimon-moderator-badge id.
std::string roleBadgeId(std::string const& roleId);

// Packed sprite when shipped, else a colored pill; no extra art needed.
cocos2d::CCNode* createRoleBadgeNode(std::string const& roleId, float targetHeight);

// One clickable badge per role, idempotent; vip suppressed, admin beats mod.
void applyRoleBadges(
    cocos2d::CCMenu* menu,
    paimon::roles::UserRoles const& roles,
    cocos2d::CCObject* target,
    cocos2d::SEL_MenuHandler onTap,
    float targetHeight,
    cocos2d::CCNode* insertBefore = nullptr
);

// Localized title + description shown when a badge is tapped.
void showRoleBadgeInfoPopup(cocos2d::CCNode* sender);

} // namespace paimon::badges
