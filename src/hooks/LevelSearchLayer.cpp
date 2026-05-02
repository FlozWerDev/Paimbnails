#include <Geode/Geode.hpp>
#include <Geode/binding/GameLevelManager.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/LevelInfoLayer.hpp>
#include <Geode/binding/LevelManagerDelegate.hpp>
#include <Geode/binding/CustomListView.hpp>
#include <Geode/binding/TableViewCell.hpp>
#include <Geode/binding/TableViewCellDelegate.hpp>
#include <Geode/modify/LevelSearchLayer.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include "../features/community/ui/LeaderboardLayer.hpp"
#include "../features/backgrounds/services/LayerBackgroundManager.hpp"
#include "../features/transitions/services/TransitionManager.hpp"
#include "../utils/SpriteHelper.hpp"
#include "../utils/PaimonDrawNode.hpp"
#include "LevelCellContext.hpp"
#include "../framework/compat/SceneLocators.hpp"
#include "../core/RuntimeLifecycle.hpp"
#include <algorithm>
#include <array>
#include <string>

using namespace geode::prelude;

namespace {
    // Keep this preview scoped to normal level search. List search uses
    // different result objects and should continue using GD's normal flow.
    bool kEnableRealtimeSearchPreview() {
        return Mod::get()->getSettingValue<bool>("realtime-search-preview");
    }
    constexpr int kRealtimeResultCount = 4;
    constexpr float kRealtimeSearchDelay = 0.35f;
    constexpr float kPreviewFallbackWidth = 356.f;
    constexpr float kPreviewFallbackHeight = 136.f;
    constexpr float kRealtimeRowHeight = 38.f;
    constexpr float kRealtimeRowGap = 4.f;

    std::string trimQuery(gd::string const& value) {
        std::string text = value;
        auto first = text.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) return "";
        auto last = text.find_last_not_of(" \t\r\n");
        return text.substr(first, last - first + 1);
    }

    std::string shorten(std::string text, size_t maxLen) {
        if (text.size() <= maxLen) return text;
        if (maxLen <= 3) return text.substr(0, maxLen);
        return text.substr(0, maxLen - 3) + "...";
    }

    void drawRect(PaimonDrawNode* draw, CCPoint origin, CCSize size, ccColor4F fill, float borderWidth = 0.f, ccColor4F border = {0.f, 0.f, 0.f, 0.f}) {
        if (!draw || size.width <= 0.f || size.height <= 0.f) return;

        CCPoint poly[] = {
            origin,
            {origin.x + size.width, origin.y},
            {origin.x + size.width, origin.y + size.height},
            {origin.x, origin.y + size.height},
        };
        draw->drawPolygon(poly, 4, fill, borderWidth, border);
    }

    char const* difficultyName(GJGameLevel* level) {
        if (!level) return "-";
        if (level->m_stars.value() <= 0) return "Unrated";
        switch (static_cast<int>(level->m_difficulty)) {
            case 10: return "Easy";
            case 20: return "Normal";
            case 30: return "Hard";
            case 40: return "Harder";
            case 50: return "Insane";
            case 60: return "Demon";
            case 70: return "Auto";
            default: return "Rated";
        }
    }

    std::string formatCount(int value) {
        if (value >= 1000000) return fmt::format("{:.1f}m", value / 1000000.f);
        if (value >= 1000) return fmt::format("{:.1f}k", value / 1000.f);
        return fmt::format("{}", value);
    }

    class RealtimeLevelSearchPreview : public CCNode, public LevelManagerDelegate, public TableViewCellDelegate {
    public:
        static RealtimeLevelSearchPreview* create(LevelSearchLayer* owner) {
            auto ret = new RealtimeLevelSearchPreview();
            if (ret && ret->init(owner)) {
                ret->autorelease();
                return ret;
            }
            CC_SAFE_DELETE(ret);
            return nullptr;
        }

        bool init(LevelSearchLayer* owner) {
            if (!CCNode::init()) return false;
            m_owner = owner;
            this->setID("paimon-realtime-search-preview"_spr);
            buildUI();
            showIdle();
            return true;
        }

        // Destructor — ensures the CustomListView (which holds a delegate
        // pointer back to us) is removed BEFORE CCNode::~CCNode destroys
        // children. Without this, the list's cells call cellPerformedAction
        // on a dangling pointer during purgeDirector, crashing the game.
        ~RealtimeLevelSearchPreview() override {
            // Guard: if shutdown already ran, list is already gone.
            if (!m_shuttingDown) {
                m_shuttingDown = true;
                this->unschedule(schedule_selector(RealtimeLevelSearchPreview::firePendingSearch));
                clearDelegate();
                removeList();
            }
            m_owner = nullptr;
        }

        void cleanup() override {
            // During popSceneWithTransition, replaceScene() calls cleanup()
            // on the LevelSearchLayer, which propagates here. Do NOT shutdown
            // — the node will be re-entered after the transition finishes.
            // Only clear the delegate so no callbacks arrive while paused.
            clearDelegate();
            CCNode::cleanup();
        }

        void onExit() override {
            // During scene transitions, onExit()/onEnter() are called temporarily.
            // CCNode::onExit() already pauses the scheduler.
            // Only clear the delegate to prevent callbacks to a destroyed owner,
            // but don't fully shutdown — the node may be re-entered.
            clearDelegate();
            CCNode::onExit();
        }

        void onEnter() override {
            CCNode::onEnter();
            // Re-show quick-search content if we have no list (e.g. after
            // returning from a pushed LevelInfoLayer scene).
            if (!m_nativeList && m_pendingQuery.empty() && !m_refreshOnEnter) {
                setQuickSearchContentVisible(true);
            }
            if (m_refreshOnEnter || (!m_pendingQuery.empty() && !m_nativeList)) {
                m_refreshOnEnter = false;
                refreshFromCurrentInput(true);
            }
        }

        void handleTextChanged(CCTextInputNode* node) {
            if (!node || m_shuttingDown || paimon::isRuntimeShuttingDown()) return;

            m_pendingQuery = trimQuery(node->getString());
            this->unschedule(schedule_selector(RealtimeLevelSearchPreview::firePendingSearch));

            if (m_pendingQuery.empty()) {
                m_activeQuery.clear();
                m_pendingKey.clear();
                showIdle();
                return;
            }

            setStatus("Searching...");
            this->scheduleOnce(
                schedule_selector(RealtimeLevelSearchPreview::firePendingSearch),
                kRealtimeSearchDelay
            );
        }

        void refreshFromCurrentInput(bool force) {
            if (m_shuttingDown || paimon::isRuntimeShuttingDown() || !m_owner || !m_owner->m_searchInput) return;
            m_pendingQuery = trimQuery(m_owner->m_searchInput->getString());
            this->unschedule(schedule_selector(RealtimeLevelSearchPreview::firePendingSearch));

            if (m_pendingQuery.empty()) {
                m_activeQuery.clear();
                m_pendingKey.clear();
                showIdle();
                return;
            }

            if (force) {
                m_activeQuery.clear();
                m_pendingKey.clear();
            }
            setStatus("Searching...");
            this->scheduleOnce(schedule_selector(RealtimeLevelSearchPreview::firePendingSearch), 0.05f);
        }

        void loadLevelsFinished(CCArray* levels, char const* key) override {
            if (m_shuttingDown || paimon::isRuntimeShuttingDown()) return;
            if (!isCurrentKey(key)) return;
            clearDelegate();
            renderResults(levels);
        }

        void loadLevelsFailed(char const* key) override {
            if (m_shuttingDown || paimon::isRuntimeShuttingDown()) return;
            if (!isCurrentKey(key)) return;
            clearDelegate();
            clearResults();
            setStatus("No results");
        }

        void loadLevelsFinished(CCArray* levels, char const* key, int) override {
            loadLevelsFinished(levels, key);
        }

        void loadLevelsFailed(char const* key, int) override {
            loadLevelsFailed(key);
        }

        bool cellPerformedAction(TableViewCell* cell, int, CellAction action, CCNode*) override {
            if (action != CellAction::Click || !cell || !m_entries) return false;

            int index = cell->m_indexPath.m_row;
            if (index < 0 || index >= static_cast<int>(m_entries->count())) return false;

            auto level = typeinfo_cast<GJGameLevel*>(m_entries->objectAtIndex(index));
            if (!level) return false;

            openLevel(level);
            return true;
        }

        int getSelectedCellIdx() override {
            return -1;
        }

        bool shouldSnapToSelected() override {
            return false;
        }

        int getCellDelegateType() override {
            return 0;
        }

        void shutdown(bool removeChildren = true) {
            if (m_shuttingDown) return;
            m_shuttingDown = true;

            this->unschedule(schedule_selector(RealtimeLevelSearchPreview::firePendingSearch));
            clearDelegate();
            m_pendingQuery.clear();
            m_activeQuery.clear();

            // Always remove the list first — the CustomListView holds a delegate
            // pointer back to this node. If the list survives past our destructor,
            // it will call cellPerformedAction on a dangling pointer and crash.
            removeList();

            if (removeChildren) {
                this->stopAllActions();
                this->unscheduleAllSelectors();
                this->removeAllChildrenWithCleanup(true);
                m_container = nullptr;
                m_statusLabel = nullptr;
            }

            if (m_statusLabel) {
                m_statusLabel->setVisible(false);
            }
            if (m_container) {
                m_container->setVisible(false);
            }

            if (removeChildren) {
                setQuickSearchContentVisible(true);
            }
            m_owner = nullptr;
        }

    private:
        LevelSearchLayer* m_owner = nullptr;
        CCNode* m_container = nullptr;
        ScrollLayer* m_scrollLayer = nullptr;
        CustomListView* m_nativeList = nullptr;
        CCNode* m_resultsBg = nullptr;
        CCNode* m_listFrame = nullptr;
        CCNode* m_listBorderOverlay = nullptr;
        CCLabelBMFont* m_statusLabel = nullptr;
        Ref<CCArray> m_entries = nullptr;
        std::array<GJGameLevel*, kRealtimeResultCount> m_results = {};
        std::string m_pendingQuery;
        std::string m_activeQuery;
        std::string m_pendingKey;
        bool m_refreshOnEnter = false;
        bool m_shuttingDown = false;

        void buildUI() {
            auto winSize = CCDirector::sharedDirector()->getWinSize();
            auto rect = quickSearchRect();

            m_container = CCNode::create();
            m_container->setID("paimon-realtime-search-preview-container"_spr);
            m_container->setPosition(rect.origin + CCPoint{rect.size.width / 2.f, rect.size.height / 2.f});
            this->addChild(m_container, 30);

            m_statusLabel = CCLabelBMFont::create("", "bigFont.fnt");
            m_statusLabel->setScale(0.32f);
            m_statusLabel->setPosition({0.f, 0.f});
            m_container->addChild(m_statusLabel);
        }

        void firePendingSearch(float) {
            if (m_shuttingDown || paimon::isRuntimeShuttingDown() || !m_owner || m_pendingQuery.empty()) return;
            if (m_pendingQuery == m_activeQuery) return;

            clearResults();
            m_activeQuery = m_pendingQuery;

            auto searchObject = m_owner->getSearchObject(SearchType::Search, m_activeQuery);
            if (!searchObject) {
                setStatus("No results");
                return;
            }

            auto key = searchObject->getKey();
            m_pendingKey = key ? key : "";

            auto manager = GameLevelManager::get();
            if (!manager) {
                setStatus("No results");
                return;
            }

            if (auto cached = manager->getStoredOnlineLevels(searchObject->getKey())) {
                renderResults(cached);
                return;
            }

            manager->m_levelManagerDelegate = this;
            manager->getOnlineLevels(searchObject);
        }

        bool isCurrentKey(char const* key) const {
            if (m_pendingKey.empty() || !key) return false;
            return m_pendingKey == key;
        }

        void renderResults(CCArray* levels) {
            if (m_shuttingDown || paimon::isRuntimeShuttingDown()) return;
            clearResults();

            auto entries = CCArray::create();
            int index = 0;
            m_results.fill(nullptr);
            if (levels) {
                for (auto level : CCArrayExt<GJGameLevel*>(levels)) {
                    if (!level || index >= kRealtimeResultCount) continue;
                    entries->addObject(level);
                    m_results[index] = level;
                    ++index;
                }
            }

            if (index == 0) {
                setStatus("No results");
            } else {
                m_statusLabel->setString("");
                m_statusLabel->setVisible(false);
                showList(entries);
                m_container->setVisible(true);
            }
        }

        void showList(CCArray* entries) {
            if (m_shuttingDown || paimon::isRuntimeShuttingDown()) return;
            removeList();
            setQuickSearchContentVisible(false);
            m_entries = entries;

            auto rect = quickSearchRect();
            m_container->setPosition(rect.origin + CCPoint{rect.size.width / 2.f, rect.size.height / 2.f});

            float viewW = std::max(220.f, rect.size.width - 18.f);
            float viewH = std::max(92.f, rect.size.height - 12.f);
            float listW = std::max(200.f, viewW - 6.f);
            float listH = std::max(82.f, viewH - 6.f);
            constexpr float kFramePad = 3.f;

            m_listFrame = paimon::SpriteHelper::createRoundedRect(
                listW + kFramePad * 2.f,
                listH + kFramePad * 2.f,
                5.f,
                {0.005f, 0.007f, 0.012f, 0.86f},
                {0.f, 0.f, 0.f, 1.f},
                0.6f
            );
            if (m_listFrame) {
                m_listFrame->setID("paimon-realtime-results-inner-frame"_spr);
                m_listFrame->setPosition({-(listW + kFramePad * 2.f) / 2.f, -(listH + kFramePad * 2.f) / 2.f});
                m_container->addChild(m_listFrame, 1);
            }

            bool oldForceCompact = paimon::hooks::g_forceCompactLevelCells;
            paimon::hooks::g_forceCompactLevelCells = true;
            auto list = CustomListView::create(
                entries,
                this,
                listH,
                listW,
                0,
                BoomListType::Level4,
                45.f
            );
            paimon::hooks::g_forceCompactLevelCells = oldForceCompact;
            if (!list) {
                setStatus("No results");
                return;
            }

            list->setID("paimon-realtime-results-list"_spr);
            list->setPosition({-listW / 2.f, -listH / 2.f});
            m_container->addChild(list, 2);
            m_nativeList = list;
            oldForceCompact = paimon::hooks::g_forceCompactLevelCells;
            paimon::hooks::g_forceCompactLevelCells = true;
            list->setupList(0.f);
            paimon::hooks::g_forceCompactLevelCells = oldForceCompact;

            m_listBorderOverlay = paimon::SpriteHelper::createRoundedRectOutline(
                listW + 1.f,
                listH + 1.f,
                4.f,
                {0.f, 0.f, 0.f, 1.f},
                0.5f
            );
            if (m_listBorderOverlay) {
                m_listBorderOverlay->setID("paimon-realtime-results-border-overlay"_spr);
                m_listBorderOverlay->setPosition({-(listW + 1.f) / 2.f, -(listH + 1.f) / 2.f});
                m_container->addChild(m_listBorderOverlay, 3);
            }

            if (entries->count() == 0) {
                removeList();
                setStatus("No results");
            }
        }

        void drawListBackground(float width, float height) {
            auto bg = PaimonDrawNode::create();
            if (!bg) return;
            bg->setID("paimon-realtime-results-bg"_spr);
            bg->setPosition({-width / 2.f, -height / 2.f});
            drawRect(bg, {0.f, 0.f}, {width, height}, {0.03f, 0.035f, 0.055f, 0.74f}, 1.f, {1.f, 1.f, 1.f, 0.10f});
            m_container->addChild(bg, 1);
            m_resultsBg = bg;
        }

        void buildRows(float width, float contentHeight) {
            if (!m_scrollLayer || !m_scrollLayer->m_contentLayer) return;

            auto menu = CCMenu::create();
            menu->setContentSize({width, contentHeight});
            menu->setAnchorPoint({0.f, 0.f});
            menu->ignoreAnchorPointForPosition(false);
            menu->setPosition({0.f, 0.f});
            m_scrollLayer->m_contentLayer->addChild(menu, 10);

            float rowW = width - 8.f;
            float y = contentHeight - kRealtimeRowGap - kRealtimeRowHeight;
            for (int i = 0; i < kRealtimeResultCount; ++i) {
                auto level = m_results[i];
                if (!level) continue;

                auto row = PaimonDrawNode::create();
                if (row) {
                    row->setPosition({4.f, y});
                    ccColor4F fill = (i % 2 == 0)
                        ? ccColor4F{0.10f, 0.12f, 0.17f, 0.92f}
                        : ccColor4F{0.075f, 0.085f, 0.12f, 0.92f};
                    drawRect(row, {0.f, 0.f}, {rowW, kRealtimeRowHeight}, fill, 1.f, {1.f, 1.f, 1.f, 0.12f});
                    drawRect(row, {0.f, 0.f}, {3.f, kRealtimeRowHeight}, {0.50f, 0.92f, 0.36f, 0.95f});
                    m_scrollLayer->m_contentLayer->addChild(row, 0);
                }

                auto hit = CCLayerColor::create({0, 0, 0, 0}, rowW, kRealtimeRowHeight);
                auto btn = CCMenuItemSpriteExtra::create(
                    hit,
                    this,
                    menu_selector(RealtimeLevelSearchPreview::onResult)
                );
                btn->setTag(i);
                btn->setID(fmt::format("paimon-realtime-result-{}"_spr, i));
                btn->setPosition({4.f + rowW / 2.f, y + kRealtimeRowHeight / 2.f});
                menu->addChild(btn);

                addRowLabels(level, 4.f, y, rowW);
                y -= kRealtimeRowHeight + kRealtimeRowGap;
            }
        }

        void addRowLabels(GJGameLevel* level, float x, float y, float width) {
            if (!m_scrollLayer || !m_scrollLayer->m_contentLayer || !level) return;

            auto title = CCLabelBMFont::create(shorten(std::string(level->m_levelName), 24).c_str(), "bigFont.fnt");
            title->setScale(0.31f);
            title->setAnchorPoint({0.f, 0.5f});
            title->setPosition({x + 12.f, y + 25.f});
            m_scrollLayer->m_contentLayer->addChild(title, 2);

            auto author = CCLabelBMFont::create(shorten(std::string(level->m_creatorName), 18).c_str(), "chatFont.fnt");
            author->setScale(0.43f);
            author->setColor({170, 190, 210});
            author->setAnchorPoint({0.f, 0.5f});
            author->setPosition({x + 12.f, y + 11.f});
            m_scrollLayer->m_contentLayer->addChild(author, 2);

            auto meta = CCLabelBMFont::create(
                fmt::format("{}  {}*  {} dl  {} like",
                    difficultyName(level),
                    level->m_stars.value(),
                    formatCount(level->m_downloads),
                    formatCount(level->m_likes)
                ).c_str(),
                "chatFont.fnt"
            );
            meta->setScale(0.40f);
            meta->setColor({215, 225, 235});
            meta->setAnchorPoint({1.f, 0.5f});
            meta->setPosition({x + width - 10.f, y + 18.f});
            float maxMetaW = width * 0.43f;
            if (meta->getScaledContentSize().width > maxMetaW) {
                meta->setScale(meta->getScale() * maxMetaW / meta->getScaledContentSize().width);
            }
            m_scrollLayer->m_contentLayer->addChild(meta, 2);
        }

        void onResult(CCObject* sender) {
            auto item = typeinfo_cast<CCNode*>(sender);
            if (!item) return;

            int index = item->getTag();
            if (index < 0 || index >= kRealtimeResultCount) return;
            auto level = m_results[index];
            if (!level) return;

            openLevel(level);
        }

        void openLevel(GJGameLevel* level) {
            if (!level) return;

            auto manager = GameLevelManager::get();
            auto savedLevel = manager ? manager->getSavedLevel(level->m_levelID) : nullptr;
            auto levelToUse = savedLevel ? savedLevel : level;
            if (!levelToUse) return;

            auto layer = LevelInfoLayer::create(levelToUse, false);
            auto scene = CCScene::create();
            scene->addChild(layer);
            TransitionManager::get().pushScene(scene);
        }

        void setStatus(char const* text) {
            if (m_shuttingDown || paimon::isRuntimeShuttingDown() || !m_statusLabel || !m_container) return;
            auto rect = quickSearchRect();
            m_container->setPosition(rect.origin + CCPoint{rect.size.width / 2.f, rect.size.height / 2.f});
            m_statusLabel->setString(text);
            m_statusLabel->setVisible(text && text[0] != '\0');
            m_container->setVisible(true);
            removeList();
            setQuickSearchContentVisible(!(text && text[0] != '\0'));
        }

        void showIdle() {
            if (m_shuttingDown || paimon::isRuntimeShuttingDown()) return;
            clearResults();
            if (m_statusLabel) m_statusLabel->setString("");
            if (m_container) m_container->setVisible(false);
            setQuickSearchContentVisible(true);
        }

        void clearResults() {
            removeList();
        }

        void removeList() {
            if (m_nativeList) {
                m_nativeList->removeFromParentAndCleanup(true);
                m_nativeList = nullptr;
            }
            if (m_scrollLayer) {
                m_scrollLayer->removeFromParent();
                m_scrollLayer = nullptr;
            }
            if (m_resultsBg) {
                m_resultsBg->removeFromParent();
                m_resultsBg = nullptr;
            }
            if (m_listFrame) {
                m_listFrame->removeFromParent();
                m_listFrame = nullptr;
            }
            if (m_listBorderOverlay) {
                m_listBorderOverlay->removeFromParent();
                m_listBorderOverlay = nullptr;
            }
            m_entries = nullptr;
        }

        CCRect quickSearchRect() const {
            if (m_owner) {
                if (auto quickBg = m_owner->getChildByID("quick-search-bg")) {
                    auto size = quickBg->getScaledContentSize();
                    if (size.width > 0.f && size.height > 0.f) {
                        auto pos = quickBg->getPosition();
                        auto anchor = quickBg->getAnchorPoint();
                        return {
                            pos.x - size.width * anchor.x,
                            pos.y - size.height * anchor.y,
                            size.width,
                            size.height,
                        };
                    }
                }
            }

            auto winSize = CCDirector::sharedDirector()->getWinSize();
            return {
                winSize.width / 2.f - kPreviewFallbackWidth / 2.f,
                winSize.height / 2.f - 8.f,
                kPreviewFallbackWidth,
                kPreviewFallbackHeight,
            };
        }

        void setQuickSearchContentVisible(bool visible) {
            if (!m_owner) return;
            if (auto node = m_owner->getChildByID("quick-search-menu")) {
                node->setVisible(visible);
            }
        }

        void clearDelegate() {
            auto manager = GameLevelManager::get();
            if (manager && manager->m_levelManagerDelegate == this) {
                manager->m_levelManagerDelegate = nullptr;
            }
            m_pendingKey.clear();
        }
    };
}

class $modify(MyLevelSearchLayer, LevelSearchLayer) {
    static void onModify(auto& self) {
        (void)self.setHookPriorityAfterPost("LevelSearchLayer::init", "geode.node-ids");
    }

    $override
    bool init(int searchType) {
        if (!LevelSearchLayer::init(searchType)) return false;

        // fondo custom
        bool hasCustomBg = LayerBackgroundManager::get().applyBackground(this, "search");

        // si tenemos fondo, ocultar los sprites que GD pone de decoracion
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

        // Only attach realtime level results to the normal level search tab.
        // List search uses different result objects/cells; building level cells
        // there can corrupt teardown when the scene switches.
        if (kEnableRealtimeSearchPreview() && searchType == 0 && supportsRealtimePreviewUI()) {
            if (auto preview = RealtimeLevelSearchPreview::create(this)) {
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
            if (!scene->getChildByID("LevelSearchLayer")) return;
            this->onSearch(nullptr);
        });

        return true;
    }

    $override
    void onExit() {
        if (m_searchInput) {
            m_searchInput->onClickTrackNode(false);
        }
        LevelSearchLayer::onExit();
    }

    $override
    void onEnter() {
        LevelSearchLayer::onEnter();
        
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
                    if (auto preview = RealtimeLevelSearchPreview::create(this)) {
                        this->addChild(preview, 30);
                        log::debug("[LevelSearchLayer] Recreated realtime preview in onEnter()");
                    }
                }
            }
        }
    }

    $override
    void cleanup() {
        // Do NOT call destroyRealtimePreviewNow() here. During
        // popSceneWithTransition, replaceScene() triggers cleanup()
        // on this layer, which would permanently destroy the preview.
        // After the transition, onEnter() re-activates the preview.
        // The preview's own destructor handles final cleanup on shutdown.
        LevelSearchLayer::cleanup();
    }

    bool hasRealtimePreview() {
        return this->getChildByID("paimon-realtime-search-preview"_spr) != nullptr;
    }

    bool supportsRealtimePreviewUI() {
        return this->getChildByID("quick-search-menu") != nullptr
            && this->getChildByID("quick-search-bg") != nullptr;
    }

    void destroyRealtimePreviewNow() {
        if (m_searchInput) {
            m_searchInput->onClickTrackNode(false);
        }

        if (auto preview = typeinfo_cast<RealtimeLevelSearchPreview*>(
            this->getChildByID("paimon-realtime-search-preview"_spr)
        )) {
            preview->shutdown(true);
            preview->removeFromParentAndCleanup(true);
        }
    }

    $override
    void textChanged(CCTextInputNode* node) {
        LevelSearchLayer::textChanged(node);

        if (auto preview = typeinfo_cast<RealtimeLevelSearchPreview*>(
            this->getChildByID("paimon-realtime-search-preview"_spr)
        )) {
            preview->handleTextChanged(node);
        }
    }

    void refreshRealtimePreview(bool force) {
        if (auto preview = typeinfo_cast<RealtimeLevelSearchPreview*>(
            this->getChildByID("paimon-realtime-search-preview"_spr)
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

    void onLeaderboard(CCObject*) {
        destroyRealtimePreviewNow();
        TransitionManager::get().replaceScene(LeaderboardLayer::scene(LeaderboardLayer::BackTarget::LevelSearchLayer));
    }
};
