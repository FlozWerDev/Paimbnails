#include <Geode/Geode.hpp>
#include <Geode/modify/LevelListLayer.hpp>
#include "../framework/HookConventions.hpp"
#include <Geode/modify/LevelBrowserLayer.hpp>
#include "LevelCellContext.hpp"
#include "../framework/state/SessionState.hpp"
#include "../features/thumbnails/services/CompactListRefresh.hpp"
#include "../features/thumbnails/services/ThumbnailLoader.hpp"
#include "../features/thumbnails/ui/LevelCellSettingsPopup.hpp"
#include "../features/backgrounds/services/LayerBackgroundManager.hpp"
#include "../utils/SpriteHelper.hpp"
#include "../utils/HttpClient.hpp"
#include "../utils/PaimonNotification.hpp"
#include "../managers/ThumbnailAPI.hpp"
#include "../utils/FrameBudget.hpp"
#include <unordered_set>
#include <vector>

using namespace geode::prelude;

namespace {
struct LevelCellScanResult {
    std::vector<int> orderedLevelIDs;
    std::unordered_set<int> visibleLevelIDs;
    std::unordered_set<int> seen;
};

void collectLevelCellIDs(CCNode* node, LevelCellScanResult& result) {
    if (!node || !node->isVisible()) {
        if (!node) {
            return;
        }
    }

    if (auto* levelCell = typeinfo_cast<LevelCell*>(node)) {
        auto* level = levelCell->m_level;
        if (level) {
            int levelID = level->m_levelID.value();
            if (levelID > 0 && result.seen.insert(levelID).second) {
                result.orderedLevelIDs.push_back(levelID);
            }
            if (levelID > 0 && node->isVisible()) {
                result.visibleLevelIDs.insert(levelID);
            }
        }
    }

    auto* children = node->getChildren();
    if (!children) {
        return;
    }

    for (auto* child : CCArrayExt<CCNode*>(children)) {
        collectLevelCellIDs(child, result);
    }
}

std::vector<int> buildPredictiveWindow(LevelCellScanResult const& scan, size_t lookBehind, size_t lookAhead) {
    std::vector<int> predictive;
    if (scan.orderedLevelIDs.empty() || scan.visibleLevelIDs.empty()) {
        return predictive;
    }

    size_t firstVisibleIndex = scan.orderedLevelIDs.size();
    size_t lastVisibleIndex = 0;

    for (size_t index = 0; index < scan.orderedLevelIDs.size(); ++index) {
        if (!scan.visibleLevelIDs.contains(scan.orderedLevelIDs[index])) {
            continue;
        }
        firstVisibleIndex = std::min(firstVisibleIndex, index);
        lastVisibleIndex = std::max(lastVisibleIndex, index);
    }

    if (firstVisibleIndex == scan.orderedLevelIDs.size()) {
        return predictive;
    }

    size_t windowStart = firstVisibleIndex > lookBehind ? firstVisibleIndex - lookBehind : 0;
    size_t windowEnd = std::min(scan.orderedLevelIDs.size(), lastVisibleIndex + lookAhead + 1);

    predictive.reserve(windowEnd - windowStart);
    for (size_t index = windowStart; index < windowEnd; ++index) {
        int levelID = scan.orderedLevelIDs[index];
        if (scan.visibleLevelIDs.contains(levelID)) {
            continue;
        }
        predictive.push_back(levelID);
    }

    return predictive;
}

// Insert btn into the search-menu, respecting its layout if it has one;
// otherwise place it to the right of the rightmost item. Shared by the
// settings gear and refresh buttons.
void appendButtonToSearchMenu(CCMenu* searchMenu, CCNode* btn) {
    if (!searchMenu || !btn) return;

    if (searchMenu->getLayout()) {
        searchMenu->addChild(btn);
        searchMenu->updateLayout();
        return;
    }

    float rightMostX = 0.f;
    if (auto children = searchMenu->getChildren()) {
        for (auto* child : CCArrayExt<CCNode*>(children)) {
            float r = child->getPositionX() + child->getContentSize().width * child->getScaleX() * 0.5f;
            if (r > rightMostX) rightMostX = r;
        }
    }
    btn->setPosition({rightMostX + 25.f, searchMenu->getContentSize().height / 2.f});
    searchMenu->addChild(btn);
}
}

class $modify(PaimonLevelListLayer, LevelListLayer) {
    static void onModify(auto& self) {
        // Capture the list ID before init
        (void)self.setHookPriorityPre("LevelListLayer::init", geode::Priority::Normal);
    }

    $override
    bool init(GJLevelList* list) {
        if (list) {
            paimon::SessionState::get().currentListID = list->m_listID;
            log::debug("Entered List: {}", list->m_listID);
        } else {
            paimon::SessionState::get().currentListID = 0;
        }

        bool oldSuppressCompactContext = paimon::hooks::g_suppressCompactLevelCellsInContext;
        paimon::hooks::g_suppressCompactLevelCellsInContext = true;
        bool ok = LevelListLayer::init(list);
        paimon::hooks::g_suppressCompactLevelCellsInContext = oldSuppressCompactContext;
        if (ok) {
            LayerBackgroundManager::get().applyVanillaBackgroundTintFix(this);
        }
        return ok;
    }
};

class $modify(ContextTrackingBrowser, LevelBrowserLayer) {
    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "LevelBrowserLayer::init");
    }

    struct Fields {
        // IDs whose manifest was already requested
        std::unordered_set<int> m_manifestFetchedIds;
        Ref<CCSprite> m_compactButton = nullptr;
        float m_prefetchInterval = 1.0f;
    };

    void setCompactButtonColor() {
        if (!m_fields->m_compactButton) return;

        auto color = Mod::get()->getSettingValue<bool>("compact-list-mode")
            ? ccc3(255, 255, 255)
            : ccc3(125, 125, 125);
        m_fields->m_compactButton->setColor(color);
    }

    void onCompactListToggle(CCObject*) {
        bool enabled = !Mod::get()->getSettingValue<bool>("compact-list-mode");
        Mod::get()->setSettingValue<bool>("compact-list-mode", enabled);
        LevelCellSettingsPopup::s_settingsVersion++;
        setCompactButtonColor();
        paimon::thumbnails::refreshActiveLevelBrowserForCompactToggle();
    }

    void addCompactToggleButton() {
        if (!Mod::get()->getSettingValue<bool>("compact-list-show-toggle")) {
            return;
        }

        if (!m_searchObject || m_searchObject->m_searchMode != 0) {
            return;
        }

        if (m_searchObject && m_searchObject->m_searchType == SearchType::MyLevels) {
            return;
        }

        auto* infoMenu = typeinfo_cast<CCMenu*>(getChildByID("info-menu"));
        if (!infoMenu) return;

        auto spr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_smallModeIcon_001.png");
        if (!spr) spr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_filterBtn_001.png");
        if (!spr) return;

        m_fields->m_compactButton = spr;
        auto btn = CCMenuItemSpriteExtra::create(
            spr,
            this,
            menu_selector(ContextTrackingBrowser::onCompactListToggle)
        );
        btn->setID("paimon-compact-list-toggle"_spr);

        if (infoMenu->getLayout()) {
            btn->setLayoutOptions(AxisLayoutOptions::create()->setRelativeScale(0.95f));
            infoMenu->addChild(btn);
            infoMenu->updateLayout();
        } else {
            btn->setPosition({0.f, 0.f});
            infoMenu->addChild(btn);
        }

        setCompactButtonColor();
    }

    $override
    bool init(GJSearchObject* p0) {
        // Clear the context on entering search
        paimon::SessionState::get().currentListID = 0;
        if (!LevelBrowserLayer::init(p0)) return false;

        LayerBackgroundManager::get().applyBackground(this, "browser");

        addSettingsGearButton();

        addCompactToggleButton();

        addRefreshButton();

        m_fields->m_prefetchInterval = 1.0f;
        this->schedule(schedule_selector(ContextTrackingBrowser::prefetchVisibleLevelCells), m_fields->m_prefetchInterval);
        this->prefetchVisibleLevelCells(0.0f);

        return true;
    }

    $override
    void onEnter() {
        LevelBrowserLayer::onEnter();
        setCompactButtonColor();
    }

    $override
    void setupLevelBrowser(CCArray* array) {
        // Suppress compact mode for MyLevels and for LevelListLayer (lists).
        // LevelListLayer derives from LevelBrowserLayer, so this runs for both.
        bool isLevelList = typeinfo_cast<LevelListLayer*>(this) != nullptr;
        bool suppressCompactForThisBrowser =
            isLevelList ||
            (m_searchObject && m_searchObject->m_searchType == SearchType::MyLevels);

        bool oldSuppressCompactContext = paimon::hooks::g_suppressCompactLevelCellsInContext;
        if (suppressCompactForThisBrowser) {
            paimon::hooks::g_suppressCompactLevelCellsInContext = true;
        }

        LevelBrowserLayer::setupLevelBrowser(array);

        paimon::hooks::g_suppressCompactLevelCellsInContext = oldSuppressCompactContext;
        setCompactButtonColor();

        if (auto* existing = getChildByIDRecursive("paimon-compact-list-toggle"_spr)) {
            existing->setVisible(Mod::get()->getSettingValue<bool>("compact-list-show-toggle"));
        }
    }

    $override
    void onExit() {
        this->unschedule(schedule_selector(ContextTrackingBrowser::prefetchVisibleLevelCells));
        LevelBrowserLayer::onExit();
    }

    CCMenuItemSpriteExtra* createGearButton(float sprScale) {
        auto spr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_optionsBtn_001.png");
        if (!spr) spr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_optionsBtn02_001.png");
        if (!spr) return nullptr;
        spr->setScale(sprScale);
        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(ContextTrackingBrowser::onLevelCellSettings));
        btn->setID("paimon-levelcell-settings-btn"_spr);
        return btn;
    }

    // Locate the top search-menu: by ID first, then heuristically (a child menu
    // in the top third of the screen). Shared by the settings and refresh buttons.
    CCMenu* findTopSearchMenu() {
        if (auto node = this->getChildByID("search-menu")) {
            if (auto* menu = typeinfo_cast<CCMenu*>(node)) return menu;
        }
        for (auto* child : CCArrayExt<CCNode*>(this->getChildren())) {
            if (auto menu = typeinfo_cast<CCMenu*>(child)) {
                if (menu->getPosition().y > CCDirector::get()->getWinSize().height * 0.7f) {
                    return menu;
                }
            }
        }
        return nullptr;
    }

    void addSettingsGearButton() {
        CCMenu* searchMenu = findTopSearchMenu();

        if (!searchMenu) {
            auto winSize = CCDirector::get()->getWinSize();
            auto gearMenu = CCMenu::create();
            gearMenu->setPosition({0, 0});
            gearMenu->setID("paimon-levelcell-settings-menu"_spr);
            this->addChild(gearMenu, 100);

            if (auto btn = createGearButton(0.45f)) {
                btn->setPosition({winSize.width - 30.f, winSize.height - 30.f});
                gearMenu->addChild(btn);
            }
            return;
        }

        auto gearBtn = createGearButton(0.5f);
        if (!gearBtn) return;
        appendButtonToSearchMenu(searchMenu, gearBtn);
    }

    void onLevelCellSettings(CCObject*) {
        auto popup = LevelCellSettingsPopup::create();
        if (!popup) return;

        popup->setOnSettingsChanged([]() {
            log::info("[LevelBrowserLayer] LevelCell settings changed, will apply on next cell load");
        });

        popup->show();
    }

    void addRefreshButton() {
        auto spr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_updateBtn_001.png");
        if (!spr) spr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_replayBtn_001.png");
        if (!spr) return;
        spr->setScale(0.7f);

        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(ContextTrackingBrowser::onRefreshThumbnails));
        btn->setID("paimon-refresh-thumbs-btn"_spr);

        if (auto* searchMenu = findTopSearchMenu()) {
            appendButtonToSearchMenu(searchMenu, btn);
        } else {
            // Own menu in the top-right corner
            auto winSize = CCDirector::get()->getWinSize();
            auto menu = CCMenu::create();
            menu->setPosition({0, 0});
            menu->setID("paimon-refresh-menu"_spr);
            btn->setPosition({winSize.width - 30.f, winSize.height - 30.f});
            menu->addChild(btn);
            this->addChild(menu, 100);
        }
    }

    void onRefreshThumbnails(CCObject*) {
        LevelCellScanResult scan;
        collectLevelCellIDs(this, scan);

        if (scan.orderedLevelIDs.empty()) {
            PaimonNotify::create("No thumbnails to refresh", geode::NotificationIcon::Warning, 2.f)->show();
            return;
        }

        auto& loader = ThumbnailLoader::get();

        // Invalidate each level in the list
        for (int levelID : scan.orderedLevelIDs) {
            loader.invalidateLevel(levelID, false);
            loader.invalidateLevel(levelID, true);
        }

        // Clear manifest tracking
        for (int levelID : scan.orderedLevelIDs) {
            m_fields->m_manifestFetchedIds.erase(levelID);
        }

        // Re-request manifest and re-download
        HttpClient::get().fetchManifest(scan.orderedLevelIDs, nullptr);
        loader.prefetchLevels(scan.orderedLevelIDs, ThumbnailLoader::PriorityVisibleCell);

        auto msg = fmt::format("Refreshing {} thumbnails...", scan.orderedLevelIDs.size());
        PaimonNotify::create(msg, geode::NotificationIcon::Info, 2.f)->show();
        log::info("[LevelBrowserLayer] Refreshing {} thumbnails", scan.orderedLevelIDs.size());
    }

    void prefetchVisibleLevelCells(float) {
        auto& loader = ThumbnailLoader::get();
        if (loader.getActiveTaskCount() >= loader.getMaxConcurrentTasks()) {
            m_fields->m_prefetchInterval = std::min(m_fields->m_prefetchInterval + 0.25f, 2.0f);
        } else if (paimon::framebudget::remainingUs() < 400) {
            m_fields->m_prefetchInterval = std::min(m_fields->m_prefetchInterval + 0.15f, 2.0f);
        } else {
            m_fields->m_prefetchInterval = std::max(m_fields->m_prefetchInterval - 0.1f, 1.0f);
        }
        this->unschedule(schedule_selector(ContextTrackingBrowser::prefetchVisibleLevelCells));
        this->schedule(
            schedule_selector(ContextTrackingBrowser::prefetchVisibleLevelCells),
            m_fields->m_prefetchInterval
        );

        LevelCellScanResult scan;
        collectLevelCellIDs(this, scan);

        auto& visibleLevelIDs = scan.visibleLevelIDs;
        std::vector<int> levelIDs;
        levelIDs.reserve(visibleLevelIDs.size());
        for (int levelID : scan.orderedLevelIDs) {
            if (visibleLevelIDs.contains(levelID)) {
                levelIDs.push_back(levelID);
            }
        }

        if (levelIDs.empty()) {
            return;
        }

        auto predictiveIDs = buildPredictiveWindow(scan, 2, 5);

        // Collect IDs without a manifest for prefetch
        std::vector<int> newManifestIds;
        for (int id : levelIDs) {
            if (m_fields->m_manifestFetchedIds.find(id) == m_fields->m_manifestFetchedIds.end()) {
                newManifestIds.push_back(id);
            }
        }
        for (int id : predictiveIDs) {
            if (m_fields->m_manifestFetchedIds.find(id) == m_fields->m_manifestFetchedIds.end()) {
                newManifestIds.push_back(id);
            }
        }

        if (!newManifestIds.empty()) {
            // Mark as requested to avoid duplicates
            for (int id : newManifestIds) {
                m_fields->m_manifestFetchedIds.insert(id);
            }

            // Request the manifest once for all visible levels
            HttpClient::get().fetchManifest(newManifestIds, nullptr);
        }

        loader.prefetchLevels(levelIDs, ThumbnailLoader::PriorityVisiblePrefetch);
        if (!predictiveIDs.empty()) {
            loader.prefetchLevels(predictiveIDs, ThumbnailLoader::PriorityPredictivePrefetch);
        }

        // Warm the hero URL only if the cell thumb isn't in RAM yet (LevelInfo
        // uses tryGetCachedTexture; avoids duplicate decode/upload in m_urlTasks).
        for (int levelID : levelIDs) {
            if (loader.isLoaded(levelID, false)) continue;
            std::string url = ThumbnailAPI::get().getThumbnailURL(levelID);
            if (url.empty()) continue;
            loader.requestUrlLoad(
                url,
                [](cocos2d::CCTexture2D*, bool) {},
                ThumbnailLoader::PriorityVisiblePrefetch
            );
        }
    }

};

// NOTE: current-list-id cleanup is in MenuLayer::init
