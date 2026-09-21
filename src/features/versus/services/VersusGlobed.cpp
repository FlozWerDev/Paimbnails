#include "VersusGlobed.hpp"

#include <Geode/Geode.hpp>

#include <algorithm>

#if __has_include(<globed/soft-link/API.hpp>)
    #include <globed/soft-link/API.hpp>
    #define PAIMON_VERSUS_GLOBED 1
#endif

using namespace geode::prelude;

namespace paimon::versus::gl {

namespace {

// The shield is granted through Globed but its lifetime is ours: the API has no
// way to ask whether safe mode is still armed for our own reason.
bool s_shield = false;
bool s_isolated = false;
int s_rivalId = 0;

#ifdef PAIMON_VERSUS_GLOBED
bool tableUp() {
    return globed::api::available() && globed::api::isAtLeast("2.2.0");
}
#endif

} // namespace

bool compiled() {
#ifdef PAIMON_VERSUS_GLOBED
    return true;
#else
    return false;
#endif
}

bool present() {
#ifdef PAIMON_VERSUS_GLOBED
    return tableUp();
#else
    return false;
#endif
}

bool connected() {
#ifdef PAIMON_VERSUS_GLOBED
    return tableUp() && globed::api::net::isConnected();
#else
    return false;
#endif
}

bool inSession() {
#ifdef PAIMON_VERSUS_GLOBED
    return tableUp() && globed::api::game::isActive();
#else
    return false;
#endif
}

uint32_t pingMs() {
#ifdef PAIMON_VERSUS_GLOBED
    if (!tableUp()) return 0;
    auto const game = globed::api::net::getGamePingMs();
    return game != 0 ? game : globed::api::net::getCentralPingMs();
#else
    return 0;
#endif
}

std::vector<int> sessionPlayers() {
#ifdef PAIMON_VERSUS_GLOBED
    if (!tableUp()) return {};
    return globed::api::game::getPlayerIds();
#else
    return {};
#endif
}

bool rivalInSession(int accountId) {
    auto const ids = sessionPlayers();
    return std::find(ids.begin(), ids.end(), accountId) != ids.end();
}

std::string rivalName(int accountId) {
#ifdef PAIMON_VERSUS_GLOBED
    if (!tableUp()) return {};
    if (auto* player = globed::api::game::getPlayer(accountId)) {
        return globed::api::player::getUsername(player);
    }
#else
    (void) accountId;
#endif
    return {};
}

void isolateRival(int accountId) {
#ifdef PAIMON_VERSUS_GLOBED
    if (!tableUp()) return;
    for (auto const& player : globed::api::game::getPlayers()) {
        if (!player) continue;
        bool const isRival = globed::api::player::getAccountId(player.get()) == accountId;
        globed::api::player::setForceHide(player.get(), !isRival);
    }
    s_isolated = true;
    s_rivalId = accountId;
#else
    (void) accountId;
#endif
}

void restoreVisibility() {
#ifdef PAIMON_VERSUS_GLOBED
    if (!s_isolated || !tableUp()) {
        s_isolated = false;
        return;
    }
    for (auto const& player : globed::api::game::getPlayers()) {
        if (player) globed::api::player::setForceHide(player.get(), false);
    }
#endif
    s_isolated = false;
}

void setRivalHidden(bool hidden) {
#ifdef PAIMON_VERSUS_GLOBED
    if (!tableUp()) return;
    for (auto const& player : globed::api::game::getPlayers()) {
        if (!player) continue;
        if (globed::api::player::getAccountId(player.get()) != s_rivalId) continue;
        globed::api::player::setForceHide(player.get(), hidden);
    }
#else
    (void) hidden;
#endif
}

void grantShield() {
#ifdef PAIMON_VERSUS_GLOBED
    if (!tableUp()) return;
    globed::api::game::enableSafeMode();
    s_shield = true;
#endif
}

bool shieldActive() {
    return s_shield;
}

void clearShield() {
#ifdef PAIMON_VERSUS_GLOBED
    if (s_shield && tableUp()) globed::api::game::resetSafeMode();
#endif
    s_shield = false;
}

void respawn(bool fullReset) {
#ifdef PAIMON_VERSUS_GLOBED
    if (tableUp()) globed::api::game::causeLocalRespawn(fullReset);
#else
    (void) fullReset;
#endif
}

bool inRoom() {
#ifdef PAIMON_VERSUS_GLOBED
    return tableUp() && globed::api::room::isInRoom();
#else
    return false;
#endif
}

uint32_t roomId() {
#ifdef PAIMON_VERSUS_GLOBED
    return tableUp() ? globed::api::room::getId() : 0;
#else
    return 0;
#endif
}

int pinnedLevel() {
#ifdef PAIMON_VERSUS_GLOBED
    if (!tableUp()) return 0;
    return globed::api::room::getPinnedLevel().levelId();
#else
    return 0;
#endif
}

} // namespace paimon::versus::gl
