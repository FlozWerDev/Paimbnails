#include <Geode/modify/CreatorLayer.hpp>
#include "../framework/HookConventions.hpp"
#include "../features/backgrounds/services/LayerBackgroundManager.hpp"

using namespace geode::prelude;

class $modify(PaimonCreatorLayer, CreatorLayer) {
    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "CreatorLayer::init");
    }

    $override
    bool init() {
        if (!CreatorLayer::init()) return false;
        LayerBackgroundManager::get().applyBackground(this, "creator");
        return true;
    }
};
