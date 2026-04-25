#include <Geode/Geode.hpp>
#include <Geode/modify/EndLevelLayer.hpp>
#include "../features/foryou/services/ForYouTracker.hpp"

using namespace geode::prelude;

class $modify(ForYouEndLevelLayer, EndLevelLayer) {
    $override
    void customSetup() {
        EndLevelLayer::customSetup();

        if (auto* pl = PlayLayer::get()) {
            if (auto* level = pl->m_level) {
                paimon::foryou::ForYouTracker::get().onLevelComplete(level);
            }
        }
    }
};
