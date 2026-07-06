#pragma once

#include <Geode/Geode.hpp>
#include "RoleService.hpp"
#include <string>

namespace paimon::badges {

// Stable node ID for a role badge (mod-prefixed). "mod" maps to the historical
// "paimon-moderator-badge" id so existing lookups keep working.
std::string roleBadgeId(std::string const& roleId);

// Build a badge node for a role. Prefers a packed sprite (paim_Vip.png, etc.)
// when present, otherwise draws a clean colored pill so the feature works
// without shipping new art.
cocos2d::CCNode* createRoleBadgeNode(std::string const& roleId, float targetHeight);

// Add a clickable badge for every active role to a username menu. Idempotent:
// existing badges are left untouched, so calling it from both the cache-hit and
// network paths never duplicates. admin/mod are mutually exclusive (admin wins),
// and vip is suppressed for mods/admins since the server auto-grants it to them.
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
