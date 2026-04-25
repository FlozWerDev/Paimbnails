#include <Geode/modify/GJGarageLayer.hpp>
#include "../features/backgrounds/services/LayerBackgroundManager.hpp"

using namespace geode::prelude;

class $modify(PaimonGJGarageLayer, GJGarageLayer) {
    static void onModify(auto& self) {
        (void)self.setHookPriorityAfterPost("GJGarageLayer::init", "geode.node-ids");
    }

    $override
    bool init() {
        if (!GJGarageLayer::init()) return false;
        LayerBackgroundManager::get().applyBackground(this, "garage");
        return true;
    }
};
