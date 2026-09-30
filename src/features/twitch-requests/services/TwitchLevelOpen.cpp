#include "TwitchLevelOpen.hpp"

#include "TwitchLevelBriefCache.hpp"
#include "../TwitchRequestManager.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../../transitions/services/TransitionManager.hpp"
#include "../../../utils/MainThreadDelay.hpp"
#include "../../../utils/PaimonNotification.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/GameLevelManager.hpp>
#include <Geode/binding/LevelInfoLayer.hpp>

#include <optional>

using namespace geode::prelude;

namespace paimon::twitch {

namespace {

bool g_opening = false;
uint64_t g_openGeneration = 0;
int64_t g_currentEntryID = 0;

void pushLevelInfo(GJGameLevel* level, bool replaceScene, int64_t entryID) {
    // the saved level carries the player's progress; the searched one does not.
    if (auto* glm = GameLevelManager::get()) {
        if (auto* saved = glm->getSavedLevel(level->m_levelID)) level = saved;
    }

    auto const previousEntryID = g_currentEntryID;
    g_currentEntryID = entryID;
    auto* layer = LevelInfoLayer::create(level, false);
    if (!layer) {
        g_currentEntryID = previousEntryID;
        PaimonNotify::create("No se pudo abrir el nivel", NotificationIcon::Error)->show();
        return;
    }
    if (entryID > 0) {
        auto& manager = TwitchRequestManager::get();
        auto requests = manager.requests();
        for (size_t index = 0; index < requests.size(); ++index) {
            if (requests[index].entryID == entryID) {
                manager.markPlayed(index, level->m_normalPercent.value());
                break;
            }
        }
    }
    auto* scene = CCScene::create();
    scene->addChild(layer);

    if (replaceScene) {
        TransitionManager::get().replaceScene(scene);
    } else {
        TransitionManager::get().pushScene(scene);
    }
}

void beginOpen(int levelID, bool replaceScene, int64_t entryID) {
    if (levelID <= 0 || g_opening) return;
    auto const generation = ++g_openGeneration;

    if (auto* level = TwitchLevelBriefCache::get().peekLevel(levelID)) {
        pushLevelInfo(level, replaceScene, entryID);
        return;
    }

    g_opening = true;
    PaimonNotify::create("Buscando el nivel...", NotificationIcon::Loading)->show();
    TwitchLevelBriefCache::get().fetch(levelID, [replaceScene, entryID, generation](GJGameLevel* level) {
        if (generation != g_openGeneration) return;
        g_opening = false;
        if (paimon::isRuntimeShuttingDown()) return;
        if (!level) {
            PaimonNotify::create("Ese nivel ya no existe", NotificationIcon::Error)->show();
            return;
        }
        pushLevelInfo(level, replaceScene, entryID);
    });

    // nobody ticks the queue out here: move the clock by hand so a hung
    // lookup never blocks the button.
    paimon::scheduleMainThreadDelay(13.f, [generation] {
        if (generation != g_openGeneration) return;
        if (paimon::isRuntimeShuttingDown()) return;
        TwitchLevelBriefCache::get().tick();
        g_opening = false;
    });
}

} // namespace

void openRequestedLevel(int levelID, bool replaceScene) {
    beginOpen(levelID, replaceScene, 0);
}

void playRequestAt(size_t index, bool replaceScene) {
    if (g_opening) return;
    auto& manager = TwitchRequestManager::get();
    auto requests = manager.requests();
    if (index >= requests.size()) return;

    beginOpen(requests[index].levelID, replaceScene, requests[index].entryID);
}

std::optional<size_t> indexOfRequest(int levelID) {
    auto& manager = TwitchRequestManager::get();
    auto const requests = manager.requests();
    for (size_t index = 0; index < requests.size(); ++index) {
        if (requests[index].levelID == levelID && requests[index].entryID == g_currentEntryID) return index;
    }
    for (size_t index = 0; index < requests.size(); ++index) {
        if (requests[index].levelID == levelID && manager.inSelectedQueue(requests[index])) return index;
    }
    return std::nullopt;
}

std::optional<size_t> adjacentRequestIndex(int levelID, bool forward) {
    auto current = indexOfRequest(levelID);
    if (!current) return std::nullopt;
    auto& manager = TwitchRequestManager::get();
    auto const requests = manager.requests();
    auto index = static_cast<std::ptrdiff_t>(*current);
    for (index += forward ? 1 : -1; index >= 0 && index < static_cast<std::ptrdiff_t>(requests.size());
        index += forward ? 1 : -1) {
        auto const& request = requests[static_cast<size_t>(index)];
        if (!manager.inSelectedQueue(request)) continue;
        auto passes = requestPasses(request.levelID, !request.videoUrl.empty());
        if (passes && !*passes) continue;
        return static_cast<size_t>(index);
    }
    return std::nullopt;
}

} // namespace paimon::twitch
