#pragma once

#include <Geode/DefaultInclude.hpp>
#include <string>
#include <map>
#include <list>
#include <utility>

constexpr size_t MAX_MODERATOR_CACHE = 200;

extern std::map<std::string, std::pair<bool, bool>> g_moderatorCache;
extern std::list<std::string> g_moderatorCacheOrder;

void moderatorCacheInsert(std::string const& username, bool isMod, bool isAdmin);
bool moderatorCacheGet(std::string const& username, bool& isMod, bool& isAdmin);

void showBadgeInfoPopup(cocos2d::CCNode* sender);

