#pragma once

#include <Geode/DefaultInclude.hpp>
#include <string>
#include <map>
#include <list>
#include <utility>

// Cache global de roles de moderador/admin por username.
// Lo comparten BadgeHooks.cpp (CommentCell) y ProfilePage.cpp (ProfilePage).
// Maximo 200 entradas, se purga en orden FIFO.

constexpr size_t MAX_MODERATOR_CACHE = 200;

// guardo el rol de cada user: nombre -> {es mod, es admin}
extern std::map<std::string, std::pair<bool, bool>> g_moderatorCache;
extern std::list<std::string> g_moderatorCacheOrder;

void moderatorCacheInsert(std::string const& username, bool isMod, bool isAdmin);
bool moderatorCacheGet(std::string const& username, bool& isMod, bool& isAdmin);

// muestra el popup con info del badge (implementada en BadgeHooks.cpp)
void showBadgeInfoPopup(cocos2d::CCNode* sender);

