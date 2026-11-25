#include <Geode/Geode.hpp>
#include <Geode/modify/LevelSearchLayer.hpp>
#include <Geode/ui/NodeEvent.hpp>
#include "../framework/HookConventions.hpp"
#include "../core/modules/ModuleRegistry.hpp"
#include "../features/community/ui/LeaderboardLayer.hpp"
#include "../features/backgrounds/services/LayerBackgroundManager.hpp"
#include "../features/transitions/services/TransitionManager.hpp"
#include "../utils/SpriteHelper.hpp"
#include "../utils/ScissorClipNode.hpp"
#include "../utils/PaimonDrawNode.hpp"
#include "LevelCellContext.hpp"
#include "../framework/compat/SceneLocators.hpp"
#include "../core/RuntimeLifecycle.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "../features/level-search/services/LevelSearchHelpers.hpp"
#include "../features/level-search/services/SearchRequestCoordinator.hpp"
#include "../features/level-search/services/LevelSearchInternal.hpp"

using namespace geode::prelude;
using namespace paimon::levelsearch;

class $modify(MyLevelSearchLayer, LevelSearchLayer) {
    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "LevelSearchLayer::init");
    }

    struct Fields {
        bool m_previewCallbacksSuspended = false;
        std::unordered_map<CCMenuItem*, SEL_MenuHandler> m_nativeSearchCallbacks;
    };

    CCNode* getRealtimePreviewNodeSafe() {
        if (m_fields->m_previewCallbacksSuspended || paimon::isRuntimeShuttingDown()) {
            return nullptr;
        }
        return this->getChildByID("paimon-realtime-search-preview"_spr);
    }

    bool init(int type) {
        if (!LevelSearchLayer::init(type)) return false;
        this->addOnEnterCallback([this] {
            this->handleEnter();
            return true;
        });
        this->addOnExitCallback([this] {
            this->handleExit();
            return true;
        });
        this->addCleanupCallback([this] {
            this->handleCleanup();
            return true;
        });
        m_fields->m_previewCallbacksSuspended = false;
        wrapNativeSearchButtons(this);

        bool hasCustomBg = LayerBackgroundManager::get().applyBackground(this, "search");

        // with a custom bg, hide gd's decorative sprites
        if (hasCustomBg) {
            static char const* hideIDs[] = {
                "level-search-bg",
                "quick-search-bg",
                "difficulty-filters-bg",
                "length-filters-bg",
                nullptr
            };
            for (int i = 0; hideIDs[i]; i++) {
                if (auto node = this->getChildByID(hideIDs[i])) {
                    node->setVisible(false);
                }
            }
        }

        CCSprite* spr = CCSprite::createWithSpriteFrameName("GJ_starBtn_001.png");
        if (!paimon::SpriteHelper::isValidSprite(spr)) {
            spr = CCSprite::create("paim_Daily.png"_spr);
            if (!paimon::SpriteHelper::isValidSprite(spr)) {
                spr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_bigStar_001.png");
            }
        }
        if (!paimon::SpriteHelper::isValidSprite(spr)) {
            log::warn("Could not create leaderboard button sprite in LevelSearchLayer");
            return true;
        }

        float targetSize = 35.0f;
        float currentSize = std::max(spr->getContentWidth(), spr->getContentHeight());
        if (currentSize > 0) spr->setScale(targetSize / currentSize);

        auto btn = CCMenuItemSpriteExtra::create(
            spr,
            this,
            menu_selector(MyLevelSearchLayer::onLeaderboard)
        );
        btn->setID("paimon-leaderboard-btn"_spr);

        if (auto menu = this->getChildByID("other-filter-menu")) {
            menu->addChild(btn);
            menu->updateLayout();
        } else if (auto fallbackMenu = paimon::compat::LevelBrowserLocator::findSearchMenu(this)) {
            fallbackMenu->addChild(btn);
            fallbackMenu->updateLayout();
            log::warn("Using fallback menu locator in LevelSearchLayer");
        } else {
            log::warn("Could not find 'other-filter-menu' nor fallback menu in LevelSearchLayer");
        }

        if (kEnableRealtimeSearchPreview() && supportsRealtimePreviewUI()) {
            if (auto preview = RealtimeSearchBrowserPreview::create(this)) {
                this->addChild(preview, 30);
            }
        }

        addEventListener(KeybindSettingPressedEventV3(Mod::get(), "level-search-enter"), [this](
            Keybind const&,
            bool down,
            bool repeat,
            double
        ) {
            auto scene = CCDirector::get()->getRunningScene();
            if (!down || repeat || !scene || !this->isRunning()) return;
            if (!paimon::modules::isEnabled("paimbnails.quicksearch.browser")) return;
            if (!scene->getChildByID("LevelSearchLayer")) return;
            this->onSearch(nullptr);
        });

        return true;
    }

    void wrapNativeSearchButtons(CCNode* node) {
        if (auto* item = typeinfo_cast<CCMenuItem*>(node);
            item && item->m_pListener == this && item->m_pfnSelector) {
            m_fields->m_nativeSearchCallbacks.emplace(item, item->m_pfnSelector);
            item->setTarget(this, menu_selector(MyLevelSearchLayer::onNativeSearchButton));
        }
        for (auto* child : node->getChildrenExt<CCNode*>()) wrapNativeSearchButtons(child);
    }

    void onNativeSearchButton(CCObject* sender) {
        auto* item = typeinfo_cast<CCMenuItem*>(sender);
        auto const found = m_fields->m_nativeSearchCallbacks.find(item);
        if (found == m_fields->m_nativeSearchCallbacks.end()) return;
        destroyRealtimePreviewNow();
        (this->*found->second)(sender);
    }

    void handleExit() {
        m_fields->m_previewCallbacksSuspended = true;
        releaseSearchInputFocus(this);
    }

    void handleEnter() {
        m_fields->m_previewCallbacksSuspended = false;

        // recreate the realtime preview destroyed in transition (normal tab only)
        if (kEnableRealtimeSearchPreview() && !hasRealtimePreview() && supportsRealtimePreviewUI()) {
            if (this->getChildByID("search-input")) {
                // 0 = normal search, 1 = list search (from the tab buttons)
                auto* tabMenu = this->getChildByID("tab-menu");
                auto* listBtn = tabMenu ? tabMenu->getChildByID("list-search-btn") : nullptr;
                auto* listBtnItem = listBtn ? typeinfo_cast<CCMenuItemToggler*>(listBtn) : nullptr;
                if (!listBtnItem || !listBtnItem->isToggled()) {
                    if (auto preview = RealtimeSearchBrowserPreview::create(this)) {
                        this->addChild(preview, 30);
                    }
                }
            }
        }
    }

    void handleCleanup() {
        m_fields->m_previewCallbacksSuspended = true;
        // replacescene() fires cleanup() mid-transition; onenter() re-activates after
    }

    bool hasRealtimePreview() {
        return getRealtimePreviewNodeSafe() != nullptr;
    }

    bool supportsRealtimePreviewUI() {
        return this->getChildByID("quick-search-menu") != nullptr
            && this->getChildByID("quick-search-bg") != nullptr;
    }

    void destroyRealtimePreviewNow() {
        // grab before suspending: the safe getter returns null once suspended
        auto* node = this->getChildByID("paimon-realtime-search-preview"_spr);

        m_fields->m_previewCallbacksSuspended = true;
        releaseSearchInputFocus(this);

        if (auto preview = typeinfo_cast<RealtimeSearchBrowserPreview*>(node)) {
            preview->shutdown(true);
            preview->removeFromParentAndCleanup(true);
        }
    }

    $override
    void textChanged(CCTextInputNode* node) {
        LevelSearchLayer::textChanged(node);

        if (m_fields->m_previewCallbacksSuspended || paimon::isRuntimeShuttingDown()) return;

        if (auto preview = typeinfo_cast<RealtimeSearchBrowserPreview*>(
            getRealtimePreviewNodeSafe()
        )) {
            preview->handleTextChanged(node);
        }
    }

    void refreshRealtimePreview(bool force) {
        if (m_fields->m_previewCallbacksSuspended || paimon::isRuntimeShuttingDown()) return;

        if (auto preview = typeinfo_cast<RealtimeSearchBrowserPreview*>(
            getRealtimePreviewNodeSafe()
        )) {
            preview->refreshFromCurrentInput(force);
        }
    }

    $override
    void toggleDifficulty(CCObject* sender) {
        LevelSearchLayer::toggleDifficulty(sender);
        refreshRealtimePreview(true);
    }

    $override
    void toggleTime(CCObject* sender) {
        LevelSearchLayer::toggleTime(sender);
        refreshRealtimePreview(true);
    }

    $override
    void toggleStar(CCObject* sender) {
        LevelSearchLayer::toggleStar(sender);
        refreshRealtimePreview(true);
    }

    $override
    void demonFilterSelectClosed(int filter) {
        LevelSearchLayer::demonFilterSelectClosed(filter);
        refreshRealtimePreview(true);
    }

    $override
    void clearFilters() {
        LevelSearchLayer::clearFilters();
        refreshRealtimePreview(true);
    }

    $override
    void enterPressed(CCTextInputNode* node) {
        if (hasRealtimePreview() && node == m_searchInput && !trimQuery(node->getString()).empty()) {
            destroyRealtimePreviewNow();
            this->onSearch(nullptr);
            return;
        }
        LevelSearchLayer::enterPressed(node);
    }

    $override
    void keyBackClicked() {
        destroyRealtimePreviewNow();
        // leaving search entirely; release the cached result pages
        paimon::levelsearch::SearchRequestCoordinator::get().reset();
        LevelSearchLayer::keyBackClicked();
    }

    $override
    void onBack(CCObject* sender) {
        destroyRealtimePreviewNow();
        paimon::levelsearch::SearchRequestCoordinator::get().reset();
        LevelSearchLayer::onBack(sender);
    }

    $override
    void onSearch(CCObject* sender) {
        destroyRealtimePreviewNow();
        LevelSearchLayer::onSearch(sender);
    }

    $override
    void onSearchMode(CCObject* sender) {
        destroyRealtimePreviewNow();
        LevelSearchLayer::onSearchMode(sender);
    }

    $override
    void onSearchUser(CCObject* sender) {
        destroyRealtimePreviewNow();
        LevelSearchLayer::onSearchUser(sender);
    }















    $override
    void onFriends(CCObject* sender) {
        destroyRealtimePreviewNow();
        LevelSearchLayer::onFriends(sender);
    }



    // bindings 2.2091 have no windows address for onStarAward; the native
    // button is still covered by wrapNativeSearchButtons there.
#ifndef GEODE_IS_WINDOWS
    $override
    void onStarAward(CCObject* sender) {
        destroyRealtimePreviewNow();
        LevelSearchLayer::onStarAward(sender);
    }
#endif

    void onLeaderboard(CCObject*) {
        destroyRealtimePreviewNow();
        TransitionManager::get().replaceScene(LeaderboardLayer::scene(LeaderboardLayer::BackTarget::LevelSearchLayer));
    }
};
