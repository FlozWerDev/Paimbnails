#include <Geode/Geode.hpp>
#include <Geode/modify/EndLevelLayer.hpp>
#include "../features/foryou/services/TasteProfile.hpp"
#include "../framework/HookConventions.hpp"
#include "../core/RuntimeLifecycle.hpp"

using namespace geode::prelude;

class $modify(ForYouEndLevelLayer, EndLevelLayer) {
    static void onModify(auto& self) {
        // late: keep out of other mods' achievement/stats stacks
        paimon::hooks::afterNodeIdsOrLate(self, "EndLevelLayer::customSetup");
    }

    $override
    void customSetup() {
        EndLevelLayer::customSetup();

        // next tick: stay out of the levelcomplete achievement stack
        int levelID = 0;
        if (auto* pl = PlayLayer::get(); pl && pl->m_level) {
            levelID = pl->m_level->m_levelID.value();
        }
        if (levelID <= 0) return;

        Loader::get()->queueInMainThread([levelID]() {
            if (paimon::isRuntimeShuttingDown()) return;
            auto* pl = PlayLayer::get();
            if (!pl || !pl->m_level) return;
            if (pl->m_level->m_levelID.value() != levelID) return;
            paimon::foryou::TasteProfile::get().onLevelComplete(pl->m_level);
        });
    }
};
