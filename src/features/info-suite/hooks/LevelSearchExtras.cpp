// Advanced Search entry point on the search screen, and the client side refine
// pass applied to each page the browser loads.

#include "../InfoModule.hpp"
#include "../services/AdvancedSearch.hpp"
#include "../services/SearchObjectBuilder.hpp"
#include "../ui/AdvancedSearchPopup.hpp"
#include "../../../framework/HookConventions.hpp"
#include "../../../utils/PaimonButtonHighlighter.hpp"
#include "../../../utils/SpriteHelper.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/modify/LevelBrowserLayer.hpp>
#include <Geode/modify/LevelSearchLayer.hpp>
#include <Geode/ui/BasedButtonSprite.hpp>

using namespace geode::prelude;

namespace {

bool advancedEnabled() {
    return paimon::info::moduleEnabled("info-mod-advanced-search");
}

} // namespace

class $modify(PaimonInfoSuiteSearchLayer, LevelSearchLayer) {
    static void onModify(auto& self) {
        paimon::hooks::afterAllPaimonUiOrVeryLate(self, "LevelSearchLayer::init");
    }

    bool init(int type) {
        if (!LevelSearchLayer::init(type)) return false;
        addAdvancedButton();
        return true;
    }

    void addAdvancedButton() {
        if (!advancedEnabled()) return;
        if (this->getChildByIDRecursive("info-suite-advanced-button"_spr)) return;

        CCSprite* icon = paimon::SpriteHelper::safeCreateWithFrameName("GJ_filterIcon_001.png");
        if (!icon) icon = paimon::SpriteHelper::safeCreateWithFrameName("GJ_optionsBtn_001.png");
        if (!icon) return;
        icon->setScale(0.75f);

        auto sprite = CircleButtonSprite::create(
            icon, CircleBaseColor::Cyan, CircleBaseSize::Medium);
        if (!sprite) return;

        auto btn = CCMenuItemSpriteExtra::create(
            sprite, this, menu_selector(PaimonInfoSuiteSearchLayer::onAdvancedSearch));
        if (!btn) return;
        btn->setID("info-suite-advanced-button"_spr);
        PaimonButtonHighlighter::registerButton(btn);

        // Bottom left, next to the back button, where GD leaves free space on
        // every search screen variant.
        auto menu = CCMenu::create();
        menu->setID("info-suite-advanced-menu"_spr);
        menu->setPosition({32.f, 32.f});
        menu->addChild(btn);
        this->addChild(menu, 20);
    }

    void onAdvancedSearch(CCObject*) {
        if (auto popup = paimon::info::AdvancedSearchPopup::create()) popup->show();
    }
};

class $modify(PaimonInfoSuiteRefine, LevelBrowserLayer) {
    static void onModify(auto& self) {
        (void)self.setHookPriorityPost("LevelBrowserLayer::updateResultArray",
                                       geode::Priority::Late);
    }

    CCArray* updateResultArray(CCArray* results) {
        auto* base = LevelBrowserLayer::updateResultArray(results);
        if (!advancedEnabled() || !base || !m_searchObject) return base;

        auto const* refine = paimon::info::refineFor(paimon::info::searchKey(m_searchObject));
        if (!refine) return base;

        auto* filtered = CCArray::create();
        for (auto* item : CCArrayExt<CCObject*>(base)) {
            auto* level = typeinfo_cast<GJGameLevel*>(item);
            // Anything that is not a level (lists, users) passes through: the
            // refine filters only describe levels.
            if (!level || paimon::info::passesRefine(*refine, level)) {
                filtered->addObject(item);
            }
        }
        return filtered;
    }
};
