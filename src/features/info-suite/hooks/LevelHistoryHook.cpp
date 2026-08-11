#include "../ui/LevelHistoryPopup.hpp"
#include "../services/GDHistoryClient.hpp"
#include "../../../core/modules/ModuleRegistry.hpp"
#include "../../../framework/HookConventions.hpp"
#include "../../../utils/PaimonButtonHighlighter.hpp"
#include "../../../utils/SpriteHelper.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/modify/LevelInfoLayer.hpp>

using namespace geode::prelude;

namespace {

CCMenu* findLeftSideMenu(LevelInfoLayer* layer) {
    if (!layer) return nullptr;
    if (auto* menu = typeinfo_cast<CCMenu*>(layer->getChildByID("left-side-menu"))) {
        return menu;
    }

    static char const* const knownButtonIDs[] = {
        "like-button", "info-button", "rate-button",
        "high-object-button", "leaderboard-button", nullptr
    };

    auto children = layer->getChildren();
    if (!children) return nullptr;

    CCMenu* candidate = nullptr;
    for (auto* child : CCArrayExt<CCNode*>(children)) {
        auto* menu = typeinfo_cast<CCMenu*>(child);
        if (!menu || menu->getPositionX() >= layer->getContentSize().width * 0.5f) continue;

        for (auto const* const* id = knownButtonIDs; *id; ++id) {
            if (menu->getChildByID(*id)) return menu;
        }
        if (!candidate) candidate = menu;
    }

    return candidate && candidate->getChildrenCount() >= 3 ? candidate : nullptr;
}

} // namespace

class $modify(PaimonLevelHistoryLayer, LevelInfoLayer) {
    static void onModify(auto& self) {
        paimon::hooks::afterAllPaimonUiOrVeryLate(self, "LevelInfoLayer::init");
    }

    $override
    bool init(GJGameLevel* level, bool challenge) {
        if (!LevelInfoLayer::init(level, challenge)) return false;
        if (!paimon::modules::isEnabled(paimon::info::gdhistory::kModuleId)) return true;
        if (!level || level->m_levelID.value() <= 0) return true;

        addHistoryButton();
        return true;
    }

    void addHistoryButton() {
        if (getChildByIDRecursive("level-history-button"_spr)) return;

        auto* menu = findLeftSideMenu(this);
        if (!menu) return;

        auto icon = paimon::SpriteHelper::safeCreateWithFrameName("GJ_timeIcon_001.png");
        if (!icon) icon = paimon::SpriteHelper::safeCreateWithFrameName("GJ_infoIcon_001.png");
        if (!icon) icon = paimon::SpriteHelper::safeCreateWithFrameName("GJ_plusBtn_001.png");
        if (!icon) return;
        icon->setScale(0.8f);

        auto buttonSprite = CircleButtonSprite::create(
            icon,
            CircleBaseColor::Green,
            CircleBaseSize::Medium
        );
        if (!buttonSprite) return;

        auto button = CCMenuItemSpriteExtra::create(
            buttonSprite,
            this,
            menu_selector(PaimonLevelHistoryLayer::onLevelHistory)
        );
        if (!button) return;

        button->setID("level-history-button"_spr);
        PaimonButtonHighlighter::registerButton(button);
        menu->addChild(button);
        menu->updateLayout();
    }

    void onLevelHistory(CCObject*) {
        if (!paimon::modules::isEnabled(paimon::info::gdhistory::kModuleId) || !m_level) return;

        if (auto* popup = paimon::info::LevelHistoryPopup::create(m_level)) {
            popup->show();
        }
    }
};
