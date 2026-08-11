#pragma once

#include "../services/ModlyTypes.hpp"
#include <cocos2d.h>
#include <optional>
#include <string>

namespace paimon::compat_mods {

// Rounded avatar: the remote image when there is one, otherwise the initial on a
// colour derived from the name, same as the site's colorAvatar fallback.
cocos2d::CCNode* createAvatar(std::string const& url, bool hasImage,
                              std::string const& name, float size, float radius = 0.f);

// Rounded image slot that keeps its box and crops whatever loads into it.
cocos2d::CCNode* createImageSlot(std::string const& url, float width, float height,
                                 float radius, cocos2d::ccColor4B placeholder);

// Sealed check the site draws next to verified names: red for admin/"rojo",
// green for "verde", blue for plain verified. Empty when the user has no rank.
std::optional<cocos2d::ccColor3B> rankBadgeColor(ModlyUser const& user);

// The seal itself, tinted. Returns nullptr when the user has no rank, so
// callers can skip advancing their layout cursor.
cocos2d::CCNode* createRankSeal(ModlyUser const& user, float size = 14.f);

// Tags are always stored in Spanish; translate for the English UI.
std::string translateTag(std::string const& tag);

// Deterministic colour from a name, mirroring colorAvatar in app.js.
cocos2d::ccColor3B avatarColor(std::string const& name);

// Shrinks a label until it fits maxWidth, never scaling it up.
void fitLabelWidth(cocos2d::CCLabelBMFont* label, float maxWidth);

// Pill with a coloured background, used for alpha/beta/GDPS/tags.
cocos2d::CCNode* createPill(std::string const& text, cocos2d::ccColor3B color, float scale = 0.34f);

} // namespace paimon::compat_mods
