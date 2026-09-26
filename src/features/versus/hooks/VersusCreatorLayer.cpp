#include <Geode/Geode.hpp>
#include <Geode/modify/CreatorLayer.hpp>

#include "../ui/VersusHubLayer.hpp"
#include "../../../core/modules/ModuleRegistry.hpp"
#include "../../../framework/HookConventions.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../../transitions/services/TransitionManager.hpp"

#include <algorithm>

using namespace geode::prelude;
using namespace paimon::versus;

namespace {

constexpr char const* kModuleId = "paimbnails.versus.menu";

void fitSquare(CCNode* node, float size) {
    float const source = std::max(node->getContentSize().width, node->getContentSize().height);
    node->setScale(size / std::max(1.f, source));
}

} // namespace

class $modify(PaimonVersusCreatorLayer, CreatorLayer) {
    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "CreatorLayer::init");
    }

    $override
    bool init() {
        if (!CreatorLayer::init()) return false;
        if (!paimon::modules::isEnabled(kModuleId)) return true;

        auto* menu = this->getChildByID("creator-buttons-menu");
        if (!menu) return true;

        auto* versus = typeinfo_cast<CCMenuItemSpriteExtra*>(menu->getChildByID("versus-button"));
        if (versus) {
            // retargeting the laid-out node keeps its sprite, size, hitbox
            // and place in the row.
            versus->setTarget(this, menu_selector(PaimonVersusCreatorLayer::onPaimonVersus));
            return true;
        }

        // another mod may take the button, or a future GD may move it; add
        // our own so the entry point never disappears.
        auto* face = paimon::SpriteHelper::safeCreateWithFrameName("GJ_versusBtn_001.png");
        if (!face) return true;
        fitSquare(face, 60.f);

        auto* btn = CCMenuItemSpriteExtra::create(
            face, this, menu_selector(PaimonVersusCreatorLayer::onPaimonVersus));
        btn->setID("paimon-versus-button"_spr);
        menu->addChild(btn);
        menu->updateLayout();
        return true;
    }

    void onPaimonVersus(CCObject*) {
        auto* scene = VersusHubLayer::scene();
        if (!scene) return;
        TransitionManager::get().pushScene(scene);
    }
};
