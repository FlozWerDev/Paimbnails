#include "services/DiscordPresenceManager.hpp"

#include <Geode/Geode.hpp>

using namespace geode::prelude;

$on_game(Loaded) {
    paimon::discord::DiscordPresenceManager::get().init();
}
