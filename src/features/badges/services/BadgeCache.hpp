#pragma once

#include <Geode/DefaultInclude.hpp>
#include <string>
#include <map>
#include <list>
#include <utility>

// Global cache of moderator/admin roles by username, shared by BadgeHooks.cpp
// (CommentCell) and ProfilePage.cpp. Max 200 entries, FIFO eviction.

constexpr size_t MAX_MODERATOR_CACHE = 200;

// role per user: name -> {isMod, isAdmin}
extern std::map<std::string, std::pair<bool, bool>> g_moderatorCache;
extern std::list<std::string> g_moderatorCacheOrder;

void moderatorCacheInsert(std::string const& username, bool isMod, bool isAdmin);
bool moderatorCacheGet(std::string const& username, bool& isMod, bool& isAdmin);

// shows the badge info popup (implemented in BadgeHooks.cpp)
void showBadgeInfoPopup(cocos2d::CCNode* sender);

