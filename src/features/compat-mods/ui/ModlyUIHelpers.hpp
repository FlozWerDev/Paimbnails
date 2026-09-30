#pragma once

#include "../services/ModlyTypes.hpp"
#include <cocos2d.h>
#include <optional>
#include <string>

namespace paimon::compat_mods {

// rounded avatar: remote image or initial on name-derived color (site coloravatar fallback).
cocos2d::CCNode* createAvatar(std::string const& url, bool hasImage,
                              std::string const& name, float size, float radius = 0.f);

// rounded image slot that keeps its box and crops whatever loads into it.
cocos2d::CCNode* createImageSlot(std::string const& url, float width, float height,
                                 float radius, cocos2d::ccColor4B placeholder);

// verified seal: red admin/rojo, green verde, blue verified; empty when unranked.
std::optional<cocos2d::ccColor3B> rankBadgeColor(ModlyUser const& user);

// tinted seal, nullptr when unranked so callers skip layout advance.
cocos2d::CCNode* createRankSeal(ModlyUser const& user, float size = 14.f);

// tags are always stored in spanish; translate for the english ui.
std::string translateTag(std::string const& tag);

// deterministic colour from a name, mirroring coloravatar in app.js.
cocos2d::ccColor3B avatarColor(std::string const& name);

// shrinks a label until it fits maxwidth, never scaling it up.
void fitLabelWidth(cocos2d::CCLabelBMFont* label, float maxWidth);

// pill with a coloured background, used for alpha/beta/gdps/tags.
cocos2d::CCNode* createPill(std::string const& text, cocos2d::ccColor3B color, float scale = 0.34f);

} // namespace paimon::compat_mods
