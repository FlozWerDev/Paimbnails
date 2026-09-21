#pragma once

#include <Geode/Geode.hpp>

// Tucks herself under a menu button so only her face pokes out; with the Guide
// on she waits at the Hub button and opens the chat instead.

namespace paimon::hidden_paimon {

inline constexpr char const* kModuleId = "paimbnails.hiddenpaimon.menu";

// Rebuilds her inside layer, dropping any previous one. Safe to call twice.
void attach(cocos2d::CCLayer* layer);

// Re-runs attach on whatever MenuLayer the scene is showing.
void refresh(cocos2d::CCNode* scene);

} // namespace paimon::hidden_paimon
