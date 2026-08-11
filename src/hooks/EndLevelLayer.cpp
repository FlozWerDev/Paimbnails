#include <Geode/Geode.hpp>
#include <Geode/modify/EndLevelLayer.hpp>
#include "../features/foryou/services/TasteProfile.hpp"
#include "../framework/HookConventions.hpp"
#include "../core/RuntimeLifecycle.hpp"

using namespace geode::prelude;

class $modify(ForYouEndLevelLayer, EndLevelLayer) {
    static void onModify(auto& self) {
        // Late: run after other mods so we don't appear in their achievement/stats stacks.
        paimon::hooks::afterNodeIdsOrLate(self, "EndLevelLayer::customSetup");
    }

    $override
    void customSetup() {
        EndLevelLayer::customSetup();

        // Defer the tracker to the next tick to stay out of the levelComplete
        // stack (where the game fires achievements).
        int levelID = 0;
        if (auto* pl = PlayLayer::get()) {
            if (auto* level = pl->m_level) {
                levelID = level->m_levelID.value();
            }
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
