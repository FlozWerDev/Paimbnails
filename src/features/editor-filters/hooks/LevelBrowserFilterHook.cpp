#include <Geode/Geode.hpp>
#include <Geode/modify/LevelBrowserLayer.hpp>
#include <Geode/binding/LocalLevelManager.hpp>
#include <Geode/binding/GJSearchObject.hpp>
#include <Geode/binding/GJGameLevel.hpp>

#include "../services/MyLevelFilters.hpp"
#include "../ui/MyLevelFilterPopup.hpp"
#include "../../../framework/HookConventions.hpp"

using namespace geode::prelude;

namespace {
    inline bool filtersEnabled() {
        return Mod::get()->getSettingValue<bool>("editor-filters-enable");
    }

    bool isMyLevels(GJSearchObject* obj) {
        return obj && obj->m_searchType == SearchType::MyLevels;
    }

    struct LocalLevelSwap {
        LocalLevelManager* llm;
        CCArray* original;
        Ref<CCArray> filtered;
        LocalLevelSwap(LocalLevelManager* m, CCArray* orig, CCArray* filt)
            : llm(m), original(orig), filtered(filt) {
            llm->m_localLevels = filt;
        }
        ~LocalLevelSwap() { llm->m_localLevels = original; }
        LocalLevelSwap(LocalLevelSwap const&) = delete;
        LocalLevelSwap& operator=(LocalLevelSwap const&) = delete;
    };
}

class $modify(PaimonMyLevelsFilterBrowser, LevelBrowserLayer) {
    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "LevelBrowserLayer::init");
    }

    $override
    bool init(GJSearchObject* obj) {
        if (!LevelBrowserLayer::init(obj)) return false;
        if (!filtersEnabled() || !isMyLevels(obj)) return true;

        auto menu = CCMenu::create();
        menu->setID("paim-mylevels-filter-menu"_spr);
        auto win = CCDirector::get()->getWinSize();
        menu->setPosition({28.f, win.height / 2.f});

        auto spr = CCSprite::createWithSpriteFrameName("GJ_filterIcon_001.png");
        if (spr) spr->setScale(0.9f);
        auto btn = CCMenuItemSpriteExtra::create(
            spr, this, menu_selector(PaimonMyLevelsFilterBrowser::onFilter));
        btn->setID("paim-mylevels-filter-btn"_spr);
        menu->addChild(btn);
        this->addChild(menu, 100);

        return true;
    }

    void onFilter(CCObject*) {
        paimon::editorfilters::MyLevelFilterPopup::create()->show();
    }

    $override
    void loadPage(GJSearchObject* searchObj) {
        using namespace paimon::editorfilters;

        if (filtersEnabled() && isMyLevels(searchObj) && anyActive()) {
            auto* llm = LocalLevelManager::sharedState();
            auto* original = llm ? llm->m_localLevels : nullptr;
            if (llm && original) {
                auto* filtered = CCArray::create();
                for (auto* level : CCArrayExt<GJGameLevel*>(original)) {
                    if (matches(level)) filtered->addObject(level);
                }
                LocalLevelSwap swap(llm, original, filtered);
                LevelBrowserLayer::loadPage(searchObj);
                return;
            }
        }

        LevelBrowserLayer::loadPage(searchObj);
    }
};
