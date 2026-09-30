#pragma once

#include <Geode/Geode.hpp>

// tucks herself under a menu button so only her face pokes out; with the guide
// on she waits at the hub button and opens the chat instead.

namespace paimon::hidden_paimon {

inline constexpr char const* kModuleId = "paimbnails.hiddenpaimon.menu";

// rebuilds her inside layer, dropping any previous one. safe to call twice.
void attach(cocos2d::CCLayer* layer);

// re-runs attach on whatever menulayer the scene is showing.
void refresh(cocos2d::CCNode* scene);

} // namespace paimon::hidden_paimon
