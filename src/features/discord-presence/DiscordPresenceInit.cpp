#include "services/DiscordPresenceManager.hpp"

#include <Geode/Geode.hpp>

using namespace geode::prelude;

void PaimonDiscordPresenceOnBootstrap() {
    paimon::discord::DiscordPresenceManager::get().init();
}
