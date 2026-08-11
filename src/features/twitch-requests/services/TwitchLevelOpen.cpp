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

using namespace geode::prelude;

namespace paimon::twitch {

namespace {

bool g_opening = false;

void pushLevelInfo(GJGameLevel* level, bool replaceScene) {
    // El nivel guardado trae el progreso del jugador; el de la busqueda no.
    if (auto* glm = GameLevelManager::get()) {
        if (auto* saved = glm->getSavedLevel(level->m_levelID)) level = saved;
    }

    auto* layer = LevelInfoLayer::create(level, false);
    if (!layer) {
        PaimonNotify::create("No se pudo abrir el nivel", NotificationIcon::Error)->show();
        return;
    }
    auto* scene = CCScene::create();
    scene->addChild(layer);

    if (replaceScene) {
        TransitionManager::get().replaceScene(scene);
    } else {
        TransitionManager::get().pushScene(scene);
    }
}

} // namespace

void openRequestedLevel(int levelID, bool replaceScene) {
    if (levelID <= 0 || g_opening) return;

    if (auto* level = TwitchLevelBriefCache::get().peekLevel(levelID)) {
        pushLevelInfo(level, replaceScene);
        return;
    }

    g_opening = true;
    PaimonNotify::create("Buscando el nivel...", NotificationIcon::Loading)->show();
    TwitchLevelBriefCache::get().fetch(levelID, [replaceScene](GJGameLevel* level) {
        g_opening = false;
        if (paimon::isRuntimeShuttingDown()) return;
        if (!level) {
            PaimonNotify::create("Ese nivel ya no existe", NotificationIcon::Error)->show();
            return;
        }
        pushLevelInfo(level, replaceScene);
    });

    // Fuera de la lista de pedidos nadie llama a tick(), asi que aqui movemos
    // nosotros el reloj del cache para que una busqueda colgada no deje el
    // boton bloqueado.
    paimon::scheduleMainThreadDelay(13.f, [] {
        if (paimon::isRuntimeShuttingDown()) return;
        TwitchLevelBriefCache::get().tick();
        g_opening = false;
    });
}

void playRequestAt(size_t index, bool replaceScene) {
    auto& manager = TwitchRequestManager::get();
    auto requests = manager.requests();
    if (index >= requests.size()) return;

    int const levelID = requests[index].levelID;
    int percent = 0;
    if (auto* glm = GameLevelManager::get()) {
        if (auto* saved = glm->getSavedLevel(levelID)) percent = saved->m_normalPercent.value();
    }
    manager.markPlayed(index, percent);
    openRequestedLevel(levelID, replaceScene);
}

std::optional<size_t> indexOfRequest(int levelID) {
    auto const requests = TwitchRequestManager::get().requests();
    for (size_t index = 0; index < requests.size(); ++index) {
        if (requests[index].levelID == levelID) return index;
    }
    return std::nullopt;
}

} // namespace paimon::twitch
