#include <Geode/Geode.hpp>
#include <Geode/modify/LevelBrowserLayer.hpp>
#include <Geode/binding/LocalLevelManager.hpp>
#include <Geode/binding/GJSearchObject.hpp>
#include <Geode/binding/GJGameLevel.hpp>

#include "../services/MyLevelFilters.hpp"
#include "../ui/MyLevelFilterPopup.hpp"
#include "../../editor-suite/EditorModule.hpp"
#include "../../../framework/HookConventions.hpp"

using namespace geode::prelude;

namespace {
    inline bool filtersEnabled() {
        return paimon::editor::featureEnabled("editor-filters-enable");
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
    void updateFilterButton() {
        auto icon = typeinfo_cast<CCSprite*>(
            this->getChildByIDRecursive("paim-mylevels-filter-icon"_spr));
        if (!icon) return;

        icon->setColor(paimon::editorfilters::anyActive()
            ? ccColor3B{0, 255, 127}
            : ccColor3B{255, 255, 255});
    }

    void updateEmptyState(bool show) {
        if (auto old = this->getChildByID("paim-mylevels-filter-empty"_spr)) {
            old->removeFromParent();
        }
        if (!show) return;

        auto size = this->getContentSize();
        auto panel = CCScale9Sprite::create("square02b_001.png");
        panel->setID("paim-mylevels-filter-empty"_spr);
        panel->setContentSize({360.f, 72.f});
        panel->setColor({0, 0, 0});
        panel->setOpacity(255);
        panel->setPosition({size.width / 2.f, size.height / 2.f});

        auto title = CCLabelBMFont::create("NO LEVELS MATCH", "bigFont.fnt");
        title->setScale(0.52f);
        title->setPosition({180.f, 54.f});
        panel->addChild(title);

        auto hint = CCLabelBMFont::create("Adjust or clear the active filters", "chatFont.fnt");
        hint->setScale(0.6f);
        hint->setPosition({180.f, 36.f});
        panel->addChild(hint);

        auto button = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("CHANGE FILTERS", "goldFont.fnt", "GJ_button_01.png", 0.55f),
            this, menu_selector(PaimonMyLevelsFilterBrowser::onFilter));
        auto menu = CCMenu::create();
        menu->setPosition({180.f, 15.f});
        menu->addChild(button);
        panel->addChild(menu);

        this->addChild(panel, 100);
    }

    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "LevelBrowserLayer::init");
    }

    $override
    bool init(GJSearchObject* obj) {
        if (!LevelBrowserLayer::init(obj)) return false;
        if (!filtersEnabled() || !isMyLevels(obj)) return true;

        auto pageMenu = this->getChildByID("page-menu");
        if (!pageMenu) return true;

        auto spr = CCSprite::createWithSpriteFrameName("GJ_filterIcon_001.png");
        if (spr) spr->setScale(0.9f);
        spr->setID("paim-mylevels-filter-icon"_spr);
        auto btn = CCMenuItemSpriteExtra::create(
            spr, this, menu_selector(PaimonMyLevelsFilterBrowser::onFilter));
        btn->setID("paim-mylevels-filter-btn"_spr);
        pageMenu->addChild(btn);
        pageMenu->updateLayout();
        updateFilterButton();

        return true;
    }

    void onFilter(CCObject*) {
        if (!filtersEnabled()) return;
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
                updateFilterButton();
                updateEmptyState(filtered->count() == 0);
                return;
            }
        }

        LevelBrowserLayer::loadPage(searchObj);
        updateFilterButton();
        updateEmptyState(false);
    }
};
