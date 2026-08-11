#include <Geode/Geode.hpp>
#include <Geode/modify/LevelSearchLayer.hpp>
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

using namespace geode::prelude;

#include "../features/level-search/services/LevelSearchHelpers.hpp"
using namespace paimon::levelsearch;

#include "../features/level-search/services/LevelSearchInternal.hpp"

class $modify(MyLevelSearchLayer, LevelSearchLayer) {
    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "LevelSearchLayer::init");
    }

    struct Fields {
        bool m_previewCallbacksSuspended = false;
    };

    CCNode* getRealtimePreviewNodeSafe() {
        if (m_fields->m_previewCallbacksSuspended || paimon::isRuntimeShuttingDown()) {
            return nullptr;
        }
        return this->getChildByID("paimon-realtime-search-preview"_spr);
    }

    $override
    bool init(int searchType) {
        if (!LevelSearchLayer::init(searchType)) return false;
        m_fields->m_previewCallbacksSuspended = false;

        bool hasCustomBg = LayerBackgroundManager::get().applyBackground(this, "search");

        // With a custom bg, hide GD's decorative sprites
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
        if (currentSize > 0) {
            spr->setScale(targetSize / currentSize);
        }

        auto btn = CCMenuItemSpriteExtra::create(
            spr,
            this,
            menu_selector(MyLevelSearchLayer::onLeaderboard)
        );
        btn->setID("paimon-leaderboard-btn"_spr);

        if (auto menu = this->getChildByID("other-filter-menu")) {
            menu->addChild(btn);
            menu->updateLayout();
        } else {
            if (auto fallbackMenu = paimon::compat::LevelBrowserLocator::findSearchMenu(this)) {
                fallbackMenu->addChild(btn);
                fallbackMenu->updateLayout();
                log::warn("Using fallback menu locator in LevelSearchLayer");
            } else {
                log::warn("Could not find 'other-filter-menu' nor fallback menu in LevelSearchLayer");
            }
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

    $override
    void onExit() {
        m_fields->m_previewCallbacksSuspended = true;
        LevelSearchLayer::onExit();
        releaseSearchInputFocus(this);
    }

    $override
    void onEnter() {
        LevelSearchLayer::onEnter();
        m_fields->m_previewCallbacksSuspended = false;
        
        // Recreate realtime preview if it was destroyed during transition
        // and we're in the normal search tab (searchType == 0)
        if (kEnableRealtimeSearchPreview() && !hasRealtimePreview() && supportsRealtimePreviewUI()) {
            // Check if we're in the normal search tab by looking for the search input
            if (auto searchInput = typeinfo_cast<CCTextInputNode*>(this->getChildByID("search-input"))) {
                // Get search type from the tab buttons - 0 = normal search, 1 = list search
                int searchType = 0;
                if (auto tabMenu = this->getChildByID("tab-menu")) {
                    if (auto listBtn = tabMenu->getChildByID("list-search-btn")) {
                        if (auto listBtnItem = typeinfo_cast<CCMenuItemToggler*>(listBtn)) {
                            searchType = listBtnItem->isToggled() ? 1 : 0;
                        }
                    }
                }
                
                if (searchType == 0) {
                    if (auto preview = RealtimeSearchBrowserPreview::create(this)) {
                        this->addChild(preview, 30);
                        log::debug("[LevelSearchLayer] Recreated realtime preview in onEnter()");
                    }
                } else {
                    log::debug("[LevelSearchLayer] Skipped realtime preview recreate in list search tab");
                }
            }
        }
    }

    $override
    void cleanup() {
        m_fields->m_previewCallbacksSuspended = true;
        // Do NOT call destroyRealtimePreviewNow() here. During
        // popSceneWithTransition, replaceScene() triggers cleanup()
        // on this layer, which would permanently destroy the preview.
        // After the transition, onEnter() re-activates the preview.
        // The preview's own destructor handles final cleanup on shutdown.
        LevelSearchLayer::cleanup();
    }

    bool hasRealtimePreview() {
        return getRealtimePreviewNodeSafe() != nullptr;
    }

    bool supportsRealtimePreviewUI() {
        return this->getChildByID("quick-search-menu") != nullptr
            && this->getChildByID("quick-search-bg") != nullptr;
    }

    void destroyRealtimePreviewNow() {
        // Grab the node before suspending callbacks: getRealtimePreviewNodeSafe()
        // returns null once m_previewCallbacksSuspended is set, which would turn
        // this teardown into a no-op and leave the preview's debounced search and
        // GameLevelManager delegate alive. A late firePendingSearch would then
        // steal m_levelManagerDelegate from the LevelBrowserLayer we're opening,
        // leaving the results layer stuck loading forever.
        auto* node = this->getChildByID("paimon-realtime-search-preview"_spr);

        m_fields->m_previewCallbacksSuspended = true;
        releaseSearchInputFocus(this);

        if (auto preview = typeinfo_cast<RealtimeLevelSearchPreview*>(node)) {
            preview->shutdown(true);
            preview->removeFromParentAndCleanup(true);
            return;
        }

        if (auto preview = typeinfo_cast<RealtimeSearchBrowserPreview*>(node)) {
            preview->shutdown(true);
            preview->removeFromParentAndCleanup(true);
        }
    }

    $override
    void textChanged(CCTextInputNode* node) {
        LevelSearchLayer::textChanged(node);

        if (m_fields->m_previewCallbacksSuspended || paimon::isRuntimeShuttingDown()) {
            return;
        }

        if (auto preview = typeinfo_cast<RealtimeLevelSearchPreview*>(
            getRealtimePreviewNodeSafe()
        )) {
            preview->handleTextChanged(node);
            return;
        }

        if (auto preview = typeinfo_cast<RealtimeSearchBrowserPreview*>(
            getRealtimePreviewNodeSafe()
        )) {
            preview->handleTextChanged(node);
        }
    }

    void refreshRealtimePreview(bool force) {
        if (m_fields->m_previewCallbacksSuspended || paimon::isRuntimeShuttingDown()) {
            return;
        }

        if (auto preview = typeinfo_cast<RealtimeLevelSearchPreview*>(
            getRealtimePreviewNodeSafe()
        )) {
            preview->refreshFromCurrentInput(force);
            return;
        }

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
        LevelSearchLayer::keyBackClicked();
    }

    $override
    void onBack(CCObject* sender) {
        destroyRealtimePreviewNow();
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
    void onMostDownloaded(CCObject* sender) {
        destroyRealtimePreviewNow();
        LevelSearchLayer::onMostDownloaded(sender);
    }

    $override
    void onMostLikes(CCObject* sender) {
        destroyRealtimePreviewNow();
        LevelSearchLayer::onMostLikes(sender);
    }

    $override
    void onSuggested(CCObject* sender) {
        destroyRealtimePreviewNow();
        LevelSearchLayer::onSuggested(sender);
    }

    $override
    void onTrending(CCObject* sender) {
        destroyRealtimePreviewNow();
        LevelSearchLayer::onTrending(sender);
    }

    $override
    void onMagic(CCObject* sender) {
        destroyRealtimePreviewNow();
        LevelSearchLayer::onMagic(sender);
    }

    $override
    void onMostRecent(CCObject* sender) {
        destroyRealtimePreviewNow();
        LevelSearchLayer::onMostRecent(sender);
    }

    $override
    void onLatestStars(CCObject* sender) {
        destroyRealtimePreviewNow();
        LevelSearchLayer::onLatestStars(sender);
    }

    $override
    void onFriends(CCObject* sender) {
        destroyRealtimePreviewNow();
        LevelSearchLayer::onFriends(sender);
    }

    $override
    void onFollowed(CCObject* sender) {
        destroyRealtimePreviewNow();
        LevelSearchLayer::onFollowed(sender);
    }

    $override
    void onStarAward(CCObject* sender) {
        destroyRealtimePreviewNow();
        LevelSearchLayer::onStarAward(sender);
    }

    void onLeaderboard(CCObject*) {
        destroyRealtimePreviewNow();
        TransitionManager::get().replaceScene(LeaderboardLayer::scene(LeaderboardLayer::BackTarget::LevelSearchLayer));
    }
};
