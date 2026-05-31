#include <Geode/Geode.hpp>
#include <Geode/modify/EndLevelLayer.hpp>
#include "../features/foryou/services/ForYouTracker.hpp"

using namespace geode::prelude;

class $modify(ForYouEndLevelLayer, EndLevelLayer) {
    static void onModify(auto& self) {
        // Late: correr despues de los demas mods para no aparecer en
        // stacks de achievements/stats de otros hooks.
        (void)self.setHookPriorityPost("EndLevelLayer::customSetup", geode::Priority::Late);
    }

    $override
    void customSetup() {
        EndLevelLayer::customSetup();

        // Diferir el tracker al proximo tick para no participar del
        // stack de levelComplete (donde el juego dispara achievements).
        int levelID = 0;
        if (auto* pl = PlayLayer::get()) {
            if (auto* level = pl->m_level) {
                levelID = level->m_levelID.value();
            }
        }
        if (levelID <= 0) return;

        Loader::get()->queueInMainThread([levelID]() {
            auto* pl = PlayLayer::get();
            if (!pl || !pl->m_level) return;
            if (pl->m_level->m_levelID.value() != levelID) return;
            paimon::foryou::ForYouTracker::get().onLevelComplete(pl->m_level);
        });
    }
};
