#include <Geode/Geode.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/GJLevelList.hpp>
#include <Geode/binding/GameLevelManager.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/GJUserScore.hpp>
#include <Geode/binding/LevelBrowserLayer.hpp>
#include <Geode/binding/CustomListView.hpp>
#include <Geode/binding/LevelListLayer.hpp>
#include <Geode/binding/LevelInfoLayer.hpp>
#include <Geode/binding/LevelManagerDelegate.hpp>
#include <Geode/binding/ProfilePage.hpp>
#include <Geode/binding/SetIDPopup.hpp>
#include <Geode/binding/SetIDPopupDelegate.hpp>
#include <Geode/binding/LevelCell.hpp>
#include <Geode/binding/GJScoreCell.hpp>
#include <Geode/binding/LevelListCell.hpp>
#include <Geode/binding/TableViewCell.hpp>
#include <Geode/binding/TableViewCellDelegate.hpp>
#include <Geode/modify/LevelSearchLayer.hpp>
#include <Geode/utils/VMTHookManager.hpp>
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
#include <cmath>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace geode::prelude;

namespace {
    void releaseSearchInputFocus(LevelSearchLayer* layer) {
        if (!layer || !layer->m_searchInput) return;

        layer->m_searchInput->onClickTrackNode(false);
        if (layer->m_searchInput->m_textField) {
            layer->m_searchInput->m_textField->detachWithIME();
        }
    }

    // Keep this preview scoped to normal level search. List search uses
    // different result objects and should continue using GD's normal flow.
    bool kEnableRealtimeSearchPreview() {
        return Mod::get()->getSettingValue<bool>("realtime-search-preview");
    }
    constexpr int kRealtimeResultCount = 4;
    constexpr int kRealtimeSearchDefaultDelayMs = 350;
    constexpr int kRealtimeSearchMinDelayMs = 100;
    constexpr int kRealtimeSearchMaxDelayMs = 1000;
    constexpr float kPreviewFallbackWidth = 356.f;
    constexpr float kPreviewFallbackHeight = 136.f;
    constexpr float kRealtimeRowHeight = 38.f;
    constexpr float kRealtimeRowGap = 4.f;

    float getRealtimeSearchDelay() {
        auto delayMs = std::clamp(
            Mod::get()->getSettingValue<int>("realtime-search-debounce-ms"),
            kRealtimeSearchMinDelayMs,
            kRealtimeSearchMaxDelayMs
        );
        if (delayMs <= 0) delayMs = kRealtimeSearchDefaultDelayMs;
        return static_cast<float>(delayMs) / 1000.f;
    }

    enum class PreviewPrimaryMode {
        Levels,
        Users,
        Lists,
    };

    enum class LevelPreset {
        Search,
        Trending,
        Featured,
        Awarded,
        Magic,
        MostLiked,
        Recent,
        Downloaded,
        Followed,
        Friends,
        StarAward,
        HallOfFame,
        Bonus,
        Similar,
        UsersLevels,
    };

    enum class RequestIntent {
        Replace,
        Before,
        After,
    };

    struct LevelPresetDef {
        LevelPreset preset;
        SearchType searchType;
        char const* label;
        bool usesQuery;
        bool numericOnly;
    };

    constexpr std::array<LevelPresetDef, 15> kLevelPresetDefs = {{
        {LevelPreset::Search, SearchType::Search, "Search", true, false},
        {LevelPreset::Trending, SearchType::Trending, "Trending", false, false},
        {LevelPreset::Featured, SearchType::Featured, "Featured", false, false},
        {LevelPreset::Awarded, SearchType::Awarded, "Awarded", false, false},
        {LevelPreset::Magic, SearchType::Magic, "Magic", false, false},
        {LevelPreset::MostLiked, SearchType::MostLiked, "Top Likes", false, false},
        {LevelPreset::Recent, SearchType::Recent, "Recent", false, false},
        {LevelPreset::Downloaded, SearchType::Downloaded, "Downloaded", false, false},
        {LevelPreset::Followed, SearchType::Followed, "Followed", false, false},
        {LevelPreset::Friends, SearchType::Friends, "Friends", false, false},
        {LevelPreset::StarAward, SearchType::StarAward, "Star Award", false, false},
        {LevelPreset::HallOfFame, SearchType::HallOfFame, "Hall", false, false},
        {LevelPreset::Bonus, SearchType::Bonus, "Bonus", false, false},
        {LevelPreset::Similar, SearchType::Similar, "Similar", true, true},
        {LevelPreset::UsersLevels, SearchType::UsersLevels, "Creator", true, true},
    }};

    constexpr int kPageJumpPopupTag = 0x5041494D;
    constexpr float kPreviewInset = 6.f;
    constexpr float kPreviewSideInset = 8.f;
    constexpr float kPreviewBottomInset = 6.f;
    constexpr float kPreviewControlTopInset = 10.f;
    constexpr float kPreviewSectionGap = 4.f;
    constexpr float kPreviewRowGap = 2.f;
    constexpr float kPreviewHeaderHeight = 12.f;
    constexpr float kPreviewListTopInset = 28.f;
    constexpr float kPreviewLevelCellHeight = 45.f;
    constexpr float kPreviewUserCellHeight = 60.f;
    constexpr float kPreviewListCellHeight = 90.f;
    constexpr float kPreviewActionLaneWidth = 28.f;
    constexpr float kPreviewAutoLoadThreshold = 28.f;
    constexpr int kPreviewMaxCachedPages = 5;

    LevelPresetDef const& getLevelPresetDef(LevelPreset preset) {
        for (auto const& def : kLevelPresetDefs) {
            if (def.preset == preset) return def;
        }
        return kLevelPresetDefs.front();
    }

    size_t getLevelPresetIndex(LevelPreset preset) {
        for (size_t i = 0; i < kLevelPresetDefs.size(); ++i) {
            if (kLevelPresetDefs[i].preset == preset) return i;
        }
        return 0;
    }

    LevelPreset cycleLevelPreset(LevelPreset preset, int direction) {
        auto index = static_cast<int>(getLevelPresetIndex(preset));
        auto count = static_cast<int>(kLevelPresetDefs.size());
        index = (index + direction) % count;
        if (index < 0) index += count;
        return kLevelPresetDefs[static_cast<size_t>(index)].preset;
    }

    bool isNumericQuery(std::string const& text) {
        return !text.empty() && std::all_of(text.begin(), text.end(), [](unsigned char c) {
            return std::isdigit(c) != 0;
        });
    }

    std::vector<std::string> splitString(std::string const& value, char delimiter) {
        std::vector<std::string> parts;
        std::string current;
        for (char ch : value) {
            if (ch == delimiter) {
                parts.push_back(current);
                current.clear();
            } else {
                current.push_back(ch);
            }
        }
        parts.push_back(current);
        return parts;
    }

    void copySearchObjectState(GJSearchObject* dst, GJSearchObject* src) {
        if (!dst || !src) return;
        dst->m_searchType = src->m_searchType;
        dst->m_searchQuery = src->m_searchQuery;
        dst->m_difficulty = src->m_difficulty;
        dst->m_length = src->m_length;
        dst->m_page = src->m_page;
        dst->m_starFilter = src->m_starFilter;
        dst->m_noStarFilter = src->m_noStarFilter;
        dst->m_total = src->m_total;
        dst->m_uncompletedFilter = src->m_uncompletedFilter;
        dst->m_completedFilter = src->m_completedFilter;
        dst->m_featuredFilter = src->m_featuredFilter;
        dst->m_originalFilter = src->m_originalFilter;
        dst->m_twoPlayerFilter = src->m_twoPlayerFilter;
        dst->m_coinsFilter = src->m_coinsFilter;
        dst->m_epicFilter = src->m_epicFilter;
        dst->m_legendaryFilter = src->m_legendaryFilter;
        dst->m_mythicFilter = src->m_mythicFilter;
        dst->m_demonFilter = src->m_demonFilter;
        dst->m_folder = src->m_folder;
        dst->m_songID = src->m_songID;
        dst->m_customSongFilter = src->m_customSongFilter;
        dst->m_songFilter = src->m_songFilter;
        dst->m_searchIsOverlay = src->m_searchIsOverlay;
        dst->m_searchMode = src->m_searchMode;
    }

    char const* primaryModeLabel(PreviewPrimaryMode mode) {
        switch (mode) {
            case PreviewPrimaryMode::Levels: return "Levels";
            case PreviewPrimaryMode::Users: return "Users";
            case PreviewPrimaryMode::Lists: return "Lists";
            default: return "Levels";
        }
    }

    int parseIntSafe(std::string const& value, int fallback = 0) {
        try {
            return std::stoi(value);
        } catch (...) {
            return fallback;
        }
    }

    CCRect unionRect(CCRect const& a, CCRect const& b) {
        float minX = std::min(a.getMinX(), b.getMinX());
        float minY = std::min(a.getMinY(), b.getMinY());
        float maxX = std::max(a.getMaxX(), b.getMaxX());
        float maxY = std::max(a.getMaxY(), b.getMaxY());
        return {minX, minY, maxX - minX, maxY - minY};
    }

    CCRect nodeRect(CCNode* node) {
        if (!node) return {0.f, 0.f, 0.f, 0.f};
        auto size = node->getScaledContentSize();
        auto pos = node->getPosition();
        auto anchor = node->getAnchorPoint();
        return {
            pos.x - size.width * anchor.x,
            pos.y - size.height * anchor.y,
            size.width,
            size.height,
        };
    }

    CCRect insetRect(CCRect rect, float insetX, float insetY) {
        rect.origin.x += insetX;
        rect.origin.y += insetY;
        rect.size.width = std::max(1.f, rect.size.width - insetX * 2.f);
        rect.size.height = std::max(1.f, rect.size.height - insetY * 2.f);
        return rect;
    }

    void fitLabelWidth(CCLabelBMFont* label, float baseScale, float maxWidth) {
        if (!label) return;
        label->setScale(baseScale);
        auto width = label->getContentSize().width;
        if (width <= 0.f || maxWidth <= 0.f) return;
        auto scaledWidth = width * baseScale;
        if (scaledWidth > maxWidth) {
            label->setScale(baseScale * maxWidth / width);
        }
    }

    void visitNodeTree(CCNode* node, std::function<void(CCNode*)> const& visitor) {
        if (!node || !visitor) return;
        visitor(node);
        if (auto children = node->getChildren()) {
            for (auto* child : CCArrayExt<CCNode*>(children)) {
                visitNodeTree(child, visitor);
            }
        }
    }

    std::string toLowerCopy(std::string value) {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return value;
    }

    bool isDigitWildcardQuery(std::string const& query) {
        if (query.empty()) return false;
        int wildcardCount = 0;
        for (unsigned char c : query) {
            if (c == 'x' || c == 'X') {
                ++wildcardCount;
                continue;
            }
            if (!std::isdigit(c)) return false;
        }
        return wildcardCount == 1;
    }

    std::string objectUniqueKey(CCObject* object) {
        if (auto level = typeinfo_cast<GJGameLevel*>(object)) {
            return fmt::format("level:{}", level->m_levelID.value());
        }
        if (auto score = typeinfo_cast<GJUserScore*>(object)) {
            if (score->m_accountID > 0) return fmt::format("user:{}", score->m_accountID);
            if (score->m_userID > 0) return fmt::format("user:{}", score->m_userID);
            return fmt::format("user:{}", toLowerCopy(static_cast<std::string>(score->m_userName)));
        }
        if (auto list = typeinfo_cast<GJLevelList*>(object)) {
            return fmt::format("list:{}", list->m_listID);
        }
        return fmt::format("ptr:{}", reinterpret_cast<uintptr_t>(object));
    }

    float previewRowHeightForMode(PreviewPrimaryMode mode) {
        switch (mode) {
            case PreviewPrimaryMode::Users: return 40.f;
            case PreviewPrimaryMode::Lists: return 42.f;
            case PreviewPrimaryMode::Levels:
            default: return kPreviewLevelCellHeight;
        }
    }

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

    class RealtimePageJumpDelegate : public SetIDPopupDelegate {
    public:
        void bind(std::function<void(int)> callback) {
            m_callback = std::move(callback);
        }

        void setIDPopupClosed(SetIDPopup* popup, int value) override {
            if (!popup || popup->getTag() != kPageJumpPopupTag || !m_callback) return;
            m_callback(value);
        }

    private:
        std::function<void(int)> m_callback;
    };

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
                cancelPendingSearch();
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
            cancelPendingSearch();

            if (m_pendingQuery.empty()) {
                clearDelegate();
                m_activeQuery.clear();
                m_lastRequestFailed = false;
                showIdle();
                return;
            }

            if (m_pendingQuery == m_activeQuery && !m_lastRequestFailed) {
                return;
            }

            clearDelegate();
            m_lastRequestFailed = false;

            setStatus("Searching...");
            this->scheduleOnce(
                schedule_selector(RealtimeLevelSearchPreview::firePendingSearch),
                getRealtimeSearchDelay()
            );
        }

        void refreshFromCurrentInput(bool force) {
            if (m_shuttingDown || paimon::isRuntimeShuttingDown() || !m_owner || !m_owner->m_searchInput) return;
            m_pendingQuery = trimQuery(m_owner->m_searchInput->getString());
            cancelPendingSearch();

            if (m_pendingQuery.empty()) {
                clearDelegate();
                m_activeQuery.clear();
                m_lastRequestFailed = false;
                showIdle();
                return;
            }

            if (force || m_pendingQuery != m_activeQuery || m_lastRequestFailed) {
                clearDelegate();
            }

            if (force || m_lastRequestFailed) {
                m_activeQuery.clear();
            }

            if (!force && m_pendingQuery == m_activeQuery && !m_lastRequestFailed) {
                return;
            }

            m_lastRequestFailed = false;
            setStatus("Searching...");
            this->scheduleOnce(schedule_selector(RealtimeLevelSearchPreview::firePendingSearch), 0.05f);
        }

        void loadLevelsFinished(CCArray* levels, char const* key) override {
            if (m_shuttingDown || paimon::isRuntimeShuttingDown()) return;
            if (!isCurrentKey(key)) return;
            clearDelegate();
            m_lastRequestFailed = false;
            renderResults(levels);
        }

        void loadLevelsFailed(char const* key) override {
            if (m_shuttingDown || paimon::isRuntimeShuttingDown()) return;
            if (!isCurrentKey(key)) return;
            clearDelegate();
            clearResults();
            m_lastRequestFailed = true;
            setStatus("Search failed");
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

            cancelPendingSearch();
            clearDelegate();
            m_pendingQuery.clear();
            m_activeQuery.clear();
            m_lastRequestFailed = false;

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
        bool m_lastRequestFailed = false;

        void cancelPendingSearch() {
            this->unschedule(schedule_selector(RealtimeLevelSearchPreview::firePendingSearch));
        }

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
            if (m_pendingQuery == m_activeQuery && !m_lastRequestFailed) return;

            clearResults();
            m_activeQuery = m_pendingQuery;
            m_lastRequestFailed = false;

            auto searchObject = m_owner->getSearchObject(SearchType::Search, m_activeQuery);
            if (!searchObject) {
                m_lastRequestFailed = true;
                setStatus("Search failed");
                return;
            }

            auto key = searchObject->getKey();
            m_pendingKey = key ? key : "";
            if (m_pendingKey.empty()) {
                m_lastRequestFailed = true;
                setStatus("Search failed");
                return;
            }

            auto manager = GameLevelManager::get();
            if (!manager) {
                clearDelegate();
                m_lastRequestFailed = true;
                setStatus("Search failed");
                return;
            }

            if (auto cached = manager->getStoredOnlineLevels(searchObject->getKey())) {
                clearDelegate();
                renderResults(cached);
                return;
            }

            manager->m_levelManagerDelegate = nullptr;
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

            releaseSearchInputFocus(m_owner);

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
            m_lastRequestFailed = false;
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

    class RealtimeSearchBrowserPreview : public CCNode, public LevelManagerDelegate {
    public:
        static RealtimeSearchBrowserPreview* create(LevelSearchLayer* owner) {
            auto ret = new RealtimeSearchBrowserPreview();
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

        ~RealtimeSearchBrowserPreview() override {
            if (!m_shuttingDown) shutdown(true);
        }

        void cleanup() override {
            clearDelegate();
            CCNode::cleanup();
        }

        void onExit() override {
            clearDelegate();
            CCNode::onExit();
        }

        void onEnter() override {
            CCNode::onEnter();
            if (m_needsRefreshOnEnter) {
                m_needsRefreshOnEnter = false;
                refreshFromCurrentInput(true);
            }
        }

        void shutdown(bool removeChildren = true) {
            if (m_shuttingDown) return;
            m_shuttingDown = true;
            cancelPendingSearch();
            clearDelegate();
            m_owner = nullptr;
            m_pendingQuery.clear();
            m_activeQuery.clear();
            m_pendingKey.clear();
            m_smartBaseQuery.clear();
            m_lastRequestFailed = false;
            m_pageInfoLoaded = false;
            m_isLoading = false;
            m_totalPages = 0;
            m_currentPage = 0;
            m_requestedPage = 0;
            m_loadedPageOrder.clear();
            m_cachedPageOrder.clear();
            m_pageCache.clear();
            m_mergedRows.clear();
            m_openEntries.clear();
            m_smartQueries.clear();
            m_seenResultKeys.clear();
            m_smartQueryIndex = 0;
            m_smartSearchActive = false;
            m_smartSearchKind = SmartSearchKind::None;
            m_savedContentOffset = ccp(0.f, 0.f);
            m_savedContentHeight = 0.f;
            m_hasSavedContentOffset = false;

            if (removeChildren) {
                this->stopAllActions();
                this->unscheduleAllSelectors();
                this->removeAllChildrenWithCleanup(true);
                m_container = nullptr;
                m_statusLabel = nullptr;
                m_modeLabel = nullptr;
                m_presetLabel = nullptr;
                m_pageLabel = nullptr;
                m_countLabel = nullptr;
                m_resultsClip = nullptr;
                m_scrollLayer = nullptr;
                m_content = nullptr;
                m_controlsMenu = nullptr;
                m_prevModeBtn = nullptr;
                m_nextModeBtn = nullptr;
                m_prevPresetBtn = nullptr;
                m_nextPresetBtn = nullptr;
                m_prevPageBtn = nullptr;
                m_nextPageBtn = nullptr;
                m_pageJumpBtn = nullptr;
                m_openAllBtn = nullptr;
                m_modeCycleBtn = nullptr;
                m_presetCycleBtn = nullptr;
            }

            if (removeChildren) {
                setQuickSearchContentVisible(true);
            }
        }

        void handleTextChanged(CCTextInputNode* node) {
            if (!node || m_shuttingDown || paimon::isRuntimeShuttingDown()) return;
            m_pendingQuery = trimQuery(node->getString());
            cancelPendingSearch();

            if (m_pendingQuery.empty() && queryRequiredForCurrentMode()) {
                clearDelegate();
                m_activeQuery.clear();
                m_lastRequestFailed = false;
                clearCachedResults();
                showIdle();
                return;
            }

            if (!canSearchCurrentMode(m_pendingQuery)) {
                clearDelegate();
                clearCachedResults();
                setStatus(currentModeHint().c_str());
                return;
            }

            if (m_pendingQuery == m_activeQuery && !m_lastRequestFailed && !needsImmediateReload()) {
                return;
            }

            clearDelegate();
            setStatus("Searching...");
            this->scheduleOnce(
                schedule_selector(RealtimeSearchBrowserPreview::firePendingSearch),
                currentDebounceDelay()
            );
        }

        void refreshFromCurrentInput(bool force) {
            if (m_shuttingDown || paimon::isRuntimeShuttingDown() || !m_owner || !m_owner->m_searchInput) return;
            m_pendingQuery = trimQuery(m_owner->m_searchInput->getString());
            cancelPendingSearch();

            if (m_pendingQuery.empty() && queryRequiredForCurrentMode()) {
                clearDelegate();
                m_activeQuery.clear();
                clearCachedResults();
                showIdle();
                return;
            }

            if (!canSearchCurrentMode(m_pendingQuery)) {
                clearDelegate();
                clearCachedResults();
                setStatus(currentModeHint().c_str());
                return;
            }

            if (force) {
                m_activeQuery.clear();
                m_lastRequestFailed = false;
            }

            setStatus("Searching...");
            this->scheduleOnce(schedule_selector(RealtimeSearchBrowserPreview::firePendingSearch), force ? 0.01f : currentDebounceDelay());
        }

        void cyclePrimaryMode(int direction) {
            if (m_shuttingDown) return;
            switch (m_primaryMode) {
                case PreviewPrimaryMode::Levels:
                    m_primaryMode = direction > 0 ? PreviewPrimaryMode::Users : PreviewPrimaryMode::Lists;
                    break;
                case PreviewPrimaryMode::Users:
                    m_primaryMode = direction > 0 ? PreviewPrimaryMode::Lists : PreviewPrimaryMode::Levels;
                    break;
                case PreviewPrimaryMode::Lists:
                default:
                    m_primaryMode = direction > 0 ? PreviewPrimaryMode::Levels : PreviewPrimaryMode::Users;
                    break;
            }
            clearCachedResults();
            updateControlLabels();
            refreshFromCurrentInput(true);
        }

        void cycleLevelPreset(int direction) {
            if (m_shuttingDown || m_primaryMode != PreviewPrimaryMode::Levels) return;
            m_levelPreset = ::cycleLevelPreset(m_levelPreset, direction);
            clearCachedResults();
            updateControlLabels();
            refreshFromCurrentInput(true);
        }

        void jumpToPage(int page) {
            if (m_shuttingDown) return;
            if (page < 0) page = 0;
            if (m_totalPages > 0) page = std::min(page, m_totalPages - 1);
            loadTargetPage(page, RequestIntent::Replace, true);
        }

        void stepPage(int delta) {
            if (m_shuttingDown) return;
            jumpToPage(m_currentPage + delta);
        }

        void openFullResults() {
            auto object = buildSearchObjectForPage(m_currentPage);
            if (!object) return;
            cancelPendingSearch();
            clearDelegate();
            m_needsRefreshOnEnter = true;
            TransitionManager::get().pushScene(LevelBrowserLayer::scene(object));
        }

        void loadLevelsFinished(CCArray* levels, char const* key) override {
            if (m_shuttingDown || paimon::isRuntimeShuttingDown()) return;
            if (!isCurrentKey(key)) return;

            auto page = parsePageFromKey(key ? key : "", m_requestedPage);

            clearDelegate();
            m_isLoading = false;
            m_lastRequestFailed = false;

            if (m_smartSearchActive) {
                if (levels) {
                    for (auto* obj : CCArrayExt<CCObject*>(levels)) {
                        appendUniqueMergedObject(obj);
                    }
                }
                renderPartialSmartResults();
                requestNextSmartQuery();
                return;
            }

            if (auto manager = GameLevelManager::get()) {
                if (auto info = manager->getPageInfo(key)) {
                    applyPageInfo(info);
                }
            }

            m_currentPage = page;
            auto rows = cloneEntries(levels);
            storePage(page, rows);
            rebuildMergedRows();
            renderMergedRows();
            updateControlLabels();
        }

        void loadLevelsFailed(char const* key) override {
            if (m_shuttingDown || paimon::isRuntimeShuttingDown()) return;
            if (!isCurrentKey(key)) return;
            clearDelegate();
            m_isLoading = false;
            m_lastRequestFailed = true;
            if (m_smartSearchActive) {
                requestNextSmartQuery();
                return;
            }
            if (m_mergedRows.empty()) {
                setStatus("Search failed");
            } else {
                updateControlLabels();
            }
        }

        void loadLevelsFinished(CCArray* levels, char const* key, int) override {
            loadLevelsFinished(levels, key);
        }

        void loadLevelsFailed(char const* key, int) override {
            loadLevelsFailed(key);
        }

        void setupPageInfo(gd::string info, char const* key) override {
            if (m_shuttingDown || paimon::isRuntimeShuttingDown()) return;
            if (!isCurrentKey(key)) return;

            if (m_smartSearchActive) return;

            applyPageInfo(info);
            updateControlLabels();
        }

        void update(float) override {
            if (m_shuttingDown || !m_scrollLayer || !m_scrollLayer->m_contentLayer || !m_content) return;
            if (!m_isLoading) {
                maybeAutoLoadMore();
            }
        }

    private:
        enum class SmartSearchKind {
            None,
            LevelExact,
            LevelWildcard,
            UserVariants,
        };

        enum class EntryKind {
            Level,
            User,
            List,
        };

        struct OpenEntry {
            EntryKind kind = EntryKind::Level;
            Ref<CCObject> object;
            Ref<TableViewCell> cell;
            CCNode* wrapper = nullptr;
        };

        LevelSearchLayer* m_owner = nullptr;
        CCNode* m_container = nullptr;
        CCNode* m_resultsClip = nullptr;
        CCNode* m_content = nullptr;
        ScrollLayer* m_scrollLayer = nullptr;
        CCMenu* m_controlsMenu = nullptr;
        CCLabelBMFont* m_statusLabel = nullptr;
        CCLabelBMFont* m_modeLabel = nullptr;
        CCLabelBMFont* m_presetLabel = nullptr;
        CCLabelBMFont* m_pageLabel = nullptr;
        CCLabelBMFont* m_countLabel = nullptr;
        CCMenuItemSpriteExtra* m_prevModeBtn = nullptr;
        CCMenuItemSpriteExtra* m_nextModeBtn = nullptr;
        CCMenuItemSpriteExtra* m_prevPresetBtn = nullptr;
        CCMenuItemSpriteExtra* m_nextPresetBtn = nullptr;
        CCMenuItemSpriteExtra* m_prevPageBtn = nullptr;
        CCMenuItemSpriteExtra* m_nextPageBtn = nullptr;
        CCMenuItemSpriteExtra* m_pageJumpBtn = nullptr;
        CCMenuItemSpriteExtra* m_openAllBtn = nullptr;
        CCMenuItemSpriteExtra* m_modeCycleBtn = nullptr;
        CCMenuItemSpriteExtra* m_presetCycleBtn = nullptr;
        RealtimePageJumpDelegate m_pageJumpDelegate;
        PreviewPrimaryMode m_primaryMode = PreviewPrimaryMode::Levels;
        LevelPreset m_levelPreset = LevelPreset::Search;
        std::string m_pendingQuery;
        std::string m_activeQuery;
        std::string m_pendingKey;
        std::string m_smartBaseQuery;
        std::unordered_map<int, Ref<CCArray>> m_pageCache;
        std::vector<int> m_loadedPageOrder;
        std::vector<int> m_cachedPageOrder;
        std::vector<Ref<CCObject>> m_mergedRows;
        std::vector<OpenEntry> m_openEntries;
        std::vector<std::string> m_smartQueries;
        std::unordered_set<std::string> m_seenResultKeys;
        size_t m_smartQueryIndex = 0;
        int m_currentPage = 0;
        int m_requestedPage = 0;
        int m_totalPages = 0;
        int m_totalItems = 0;
        int m_itemsPerPage = kRealtimeResultCount;
        CCPoint m_savedContentOffset = {0.f, 0.f};
        float m_savedContentHeight = 0.f;
        bool m_pageInfoLoaded = false;
        bool m_lastRequestFailed = false;
        bool m_isLoading = false;
        bool m_hasSavedContentOffset = false;
        bool m_needsRefreshOnEnter = false;
        bool m_shuttingDown = false;
        bool m_smartSearchActive = false;
        SmartSearchKind m_smartSearchKind = SmartSearchKind::None;
        float m_frameWidth = 0.f;
        float m_frameHeight = 0.f;

        void buildUI() {
            auto rect = quickSearchRect();
            m_container = CCNode::create();
            m_container->setID("paimon-realtime-search-browser-container"_spr);
            m_container->setPosition(rect.origin + CCPoint{rect.size.width / 2.f, rect.size.height / 2.f});
            this->addChild(m_container, 35);
            setQuickSearchTitleVisible(false);

            auto frameW = std::max(1.f, rect.size.width);
            auto frameH = std::max(1.f, rect.size.height);
            m_frameWidth = frameW;
            m_frameHeight = frameH;

            m_controlsMenu = CCMenu::create();
            m_controlsMenu->setPosition({0.f, 0.f});
            m_container->addChild(m_controlsMenu, 4);

            auto headerY = frameH / 2.f + 14.f;
            constexpr float kHeaderOuterMargin = 32.f;
            constexpr float kModePresetGap = 74.f;
            constexpr float kPagerStep = 28.f;
            constexpr float kActionStep = 42.f;

            auto leftModeX = -frameW / 2.f + kHeaderOuterMargin;
            auto leftPresetX = leftModeX + kModePresetGap;
            auto rightAllX = frameW / 2.f - kHeaderOuterMargin;
            auto rightGoX = rightAllX - kActionStep;
            auto rightNextX = rightGoX - kActionStep;
            auto rightPageX = rightNextX - kPagerStep;
            auto rightPrevX = rightPageX - kPagerStep;
            auto resultsBottomY = -frameH / 2.f + 4.f;
            auto resultsHeight = std::max(64.f, frameH - 8.f);
            auto countY = resultsBottomY + 7.f;
            auto resultsCenterY = resultsBottomY + resultsHeight / 2.f;

            auto listFrame = paimon::SpriteHelper::createRoundedRect(
                frameW - 12.f,
                resultsHeight,
                6.f,
                {0.01f, 0.015f, 0.03f, 0.84f},
                {1.f, 1.f, 1.f, 0.18f},
                0.8f
            );
            if (listFrame) {
                listFrame->setPosition({-(frameW - 12.f) / 2.f, resultsBottomY});
                m_container->addChild(listFrame, 1);
            }

            m_modeLabel = CCLabelBMFont::create("", "goldFont.fnt");
            m_modeLabel->setVisible(false);
            m_container->addChild(m_modeLabel, 4);

            m_presetLabel = CCLabelBMFont::create("", "chatFont.fnt");
            m_presetLabel->setVisible(false);
            m_container->addChild(m_presetLabel, 4);

            m_modeCycleBtn = makeTextButton("Levels", 58.f, menu_selector(RealtimeSearchBrowserPreview::onNextMode));
            if (m_modeCycleBtn) {
                m_modeCycleBtn->setPosition({leftModeX, headerY});
                m_controlsMenu->addChild(m_modeCycleBtn);
            }

            m_presetCycleBtn = makeTextButton("Search", 66.f, menu_selector(RealtimeSearchBrowserPreview::onNextPreset));
            if (m_presetCycleBtn) {
                m_presetCycleBtn->setPosition({leftPresetX, headerY});
                m_controlsMenu->addChild(m_presetCycleBtn);
            }

            m_prevModeBtn = nullptr;
            m_nextModeBtn = nullptr;
            m_prevPresetBtn = nullptr;
            m_nextPresetBtn = nullptr;

            m_pageLabel = CCLabelBMFont::create("", "goldFont.fnt");
            m_pageLabel->setScale(0.30f);
            m_pageLabel->setAnchorPoint({0.5f, 0.5f});
            m_pageLabel->setPosition({rightPageX, headerY});
            m_container->addChild(m_pageLabel, 4);

            m_prevPageBtn = makeArrowButton("<", menu_selector(RealtimeSearchBrowserPreview::onPrevPage));
            m_nextPageBtn = makeArrowButton(">", menu_selector(RealtimeSearchBrowserPreview::onNextPage));
            if (m_prevPageBtn) {
                m_prevPageBtn->setPosition({rightPrevX, headerY});
                m_controlsMenu->addChild(m_prevPageBtn);
            }
            if (m_nextPageBtn) {
                m_nextPageBtn->setPosition({rightNextX, headerY});
                m_controlsMenu->addChild(m_nextPageBtn);
            }

            auto pageSpr = ButtonSprite::create("Go", 26, true, "bigFont.fnt", "GJ_button_02.png", 18.f, 0.38f);
            if (pageSpr) {
                m_pageJumpBtn = CCMenuItemSpriteExtra::create(pageSpr, this, menu_selector(RealtimeSearchBrowserPreview::onPageJump));
                m_pageJumpBtn->setPosition({rightGoX, headerY});
                m_controlsMenu->addChild(m_pageJumpBtn);
            }

            auto openSpr = ButtonSprite::create("All", 26, true, "bigFont.fnt", "GJ_button_01.png", 18.f, 0.34f);
            if (openSpr) {
                m_openAllBtn = CCMenuItemSpriteExtra::create(openSpr, this, menu_selector(RealtimeSearchBrowserPreview::onOpenAll));
                m_openAllBtn->setPosition({rightAllX, headerY});
                m_controlsMenu->addChild(m_openAllBtn);
            }

            m_countLabel = CCLabelBMFont::create("", "chatFont.fnt");
            m_countLabel->setScale(0.34f);
            m_countLabel->setAnchorPoint({0.5f, 0.5f});
            m_countLabel->setPosition({0.f, countY});
            m_container->addChild(m_countLabel, 4);

            m_statusLabel = CCLabelBMFont::create("", "bigFont.fnt");
            m_statusLabel->setScale(0.32f);
            m_statusLabel->setAnchorPoint({0.5f, 0.5f});
            m_statusLabel->setPosition({0.f, resultsCenterY});
            m_container->addChild(m_statusLabel, 4);

            auto resultsSize = CCSize{
                std::max(1.f, frameW - 18.f),
                std::max(1.f, resultsHeight - 8.f)
            };
            auto clip = CCClippingNode::create(paimon::SpriteHelper::createRoundedRectStencil(resultsSize.width, resultsSize.height, 5.f));
            if (clip) {
                m_resultsClip = clip;
            } else {
                m_resultsClip = CCNode::create();
            }
            m_resultsClip->setContentSize(resultsSize);
            m_resultsClip->setAnchorPoint({0.f, 0.f});
            m_resultsClip->ignoreAnchorPointForPosition(false);
            m_resultsClip->setPosition({-resultsSize.width / 2.f, resultsBottomY + 4.f});
            m_container->addChild(m_resultsClip, 3);

            m_scrollLayer = ScrollLayer::create(resultsSize);
            m_scrollLayer->setPosition({0.f, 0.f});
            m_resultsClip->addChild(m_scrollLayer);

            m_content = CCNode::create();
            m_content->setContentSize(resultsSize);
            m_scrollLayer->m_contentLayer->addChild(m_content);
            m_scrollLayer->m_contentLayer->setContentSize(resultsSize);

            m_pageJumpDelegate.bind([this](int value) {
                this->jumpToPage(value - 1);
            });

            this->schedule(schedule_selector(RealtimeSearchBrowserPreview::update));
            updateControlLabels();
        }

        CCMenuItemSpriteExtra* makeArrowButton(char const* text, SEL_MenuHandler handler) {
            auto spr = ButtonSprite::create(text, 20, true, "bigFont.fnt", "GJ_button_02.png", 18.f, 0.36f);
            if (!spr) return nullptr;
            auto btn = CCMenuItemSpriteExtra::create(spr, this, handler);
            btn->setSizeMult(1.0f);
            return btn;
        }

        CCMenuItemSpriteExtra* makeTextButton(char const* text, float width, SEL_MenuHandler handler) {
            auto spr = ButtonSprite::create(text, static_cast<int>(width), true, "bigFont.fnt", "GJ_button_02.png", 18.f, 0.34f);
            if (!spr) return nullptr;
            auto btn = CCMenuItemSpriteExtra::create(spr, this, handler);
            if (!btn) return nullptr;
            btn->setSizeMult(1.0f);
            return btn;
        }

        void setButtonText(CCMenuItemSpriteExtra* btn, char const* text, float width) {
            if (!btn) return;
            auto makeSprite = [text, width]() -> ButtonSprite* {
                return ButtonSprite::create(text ? text : "", static_cast<int>(width), true, "bigFont.fnt", "GJ_button_02.png", 18.f, 0.34f);
            };
            if (auto normal = makeSprite()) btn->setNormalImage(normal);
            if (auto selected = makeSprite()) btn->setSelectedImage(selected);
            if (auto disabled = makeSprite()) btn->setDisabledImage(disabled);
        }

        SmartSearchKind classifySmartSearch(std::string const& query) const {
            if (query.empty()) return SmartSearchKind::None;
            if (m_primaryMode == PreviewPrimaryMode::Users) {
                return SmartSearchKind::UserVariants;
            }
            if (m_primaryMode != PreviewPrimaryMode::Levels) {
                return SmartSearchKind::None;
            }

            auto const& def = getLevelPresetDef(m_levelPreset);
            if (!def.usesQuery) return SmartSearchKind::None;
            if (isDigitWildcardQuery(query)) return SmartSearchKind::LevelWildcard;
            if (isNumericQuery(query)) return SmartSearchKind::LevelExact;
            return SmartSearchKind::None;
        }

        std::vector<std::string> buildSmartQueries(std::string const& query) const {
            std::vector<std::string> queries;
            switch (classifySmartSearch(query)) {
                case SmartSearchKind::LevelExact:
                    queries.push_back(query);
                    break;

                case SmartSearchKind::LevelWildcard:
                    for (char digit = '0'; digit <= '9'; ++digit) {
                        auto variant = query;
                        for (auto& ch : variant) {
                            if (ch == 'x' || ch == 'X') {
                                ch = digit;
                                break;
                            }
                        }
                        queries.push_back(std::move(variant));
                    }
                    break;

                case SmartSearchKind::UserVariants:
                    queries.push_back(query);
                    for (char suffix = 'a'; suffix <= 'z'; ++suffix) {
                        queries.push_back(query + suffix);
                    }
                    for (char suffix = '0'; suffix <= '9'; ++suffix) {
                        queries.push_back(query + suffix);
                    }
                    break;

                case SmartSearchKind::None:
                default:
                    break;
            }
            return queries;
        }

        float currentDebounceDelay() const {
            return classifySmartSearch(m_pendingQuery) == SmartSearchKind::None ? getRealtimeSearchDelay() : 0.8f;
        }

        void appendUniqueMergedObject(CCObject* object) {
            if (!object) return;
            auto key = objectUniqueKey(object);
            if (!m_seenResultKeys.insert(key).second) return;
            m_mergedRows.emplace_back(object);
        }

        GJSearchObject* buildSearchObjectForQuery(std::string const& query, int page) const {
            if (!m_owner) return nullptr;
            if (m_primaryMode == PreviewPrimaryMode::Users) {
                if (query.empty()) return nullptr;
                auto object = GJSearchObject::create(SearchType::Users, query);
                if (object) object->m_page = page;
                return object;
            }
            if (m_primaryMode == PreviewPrimaryMode::Lists) {
                if (query.empty()) return nullptr;

                auto baseObject = m_owner->getSearchObject(SearchType::Search, query);
                if (!baseObject) return nullptr;

                auto object = GJSearchObject::create(SearchType::Search, query);
                if (!object) return nullptr;

                copySearchObjectState(object, baseObject);
                object->m_searchType = SearchType::Search;
                object->m_searchQuery = query;
                object->m_searchMode = 1;
                object->m_page = page;
                return object;
            }

            auto const& def = getLevelPresetDef(m_levelPreset);
            auto effectiveQuery = query;
            if (def.numericOnly && !isNumericQuery(effectiveQuery)) return nullptr;
            if (!def.usesQuery) effectiveQuery.clear();

            auto baseObject = m_owner->getSearchObject(def.searchType, effectiveQuery);
            if (!baseObject) return nullptr;
            baseObject->m_page = page;
            return baseObject;
        }

        void finalizeSmartSearch() {
            clearDelegate();
            m_isLoading = false;
            m_totalPages = m_mergedRows.empty() ? 0 : 1;
            m_totalItems = static_cast<int>(m_mergedRows.size());
            m_pageInfoLoaded = true;
            renderMergedRows();
            updateControlLabels();
        }

        void renderPartialSmartResults() {
            if (!m_smartSearchActive || m_mergedRows.empty()) return;
            m_totalPages = 1;
            m_totalItems = static_cast<int>(m_mergedRows.size());
            m_pageInfoLoaded = true;
            renderMergedRows();
            updateControlLabels();
        }

        void requestNextSmartQuery() {
            if (m_shuttingDown || paimon::isRuntimeShuttingDown()) return;
            auto manager = GameLevelManager::get();
            if (!manager) {
                finalizeSmartSearch();
                return;
            }

            while (m_smartQueryIndex < m_smartQueries.size()) {
                auto query = m_smartQueries[m_smartQueryIndex++];
                auto object = buildSearchObjectForQuery(query, 0);
                if (!object) {
                    continue;
                }

                auto key = object->getKey();
                m_pendingKey = key ? key : "";
                if (m_pendingKey.empty()) {
                    continue;
                }

                if (m_primaryMode == PreviewPrimaryMode::Levels) {
                    if (auto cached = manager->getStoredOnlineLevels(object->getKey())) {
                        for (auto* entry : CCArrayExt<CCObject*>(cached)) {
                            appendUniqueMergedObject(entry);
                        }
                        renderPartialSmartResults();
                        continue;
                    }
                }

                manager->m_levelManagerDelegate = nullptr;
                manager->m_levelManagerDelegate = this;
                m_isLoading = true;
                dispatchSearch(manager, object);
                return;
            }

            finalizeSmartSearch();
        }

        void startSmartSearch(std::string const& query) {
            m_smartBaseQuery = query;
            m_smartSearchKind = classifySmartSearch(query);
            m_smartQueries = buildSmartQueries(query);
            m_smartQueryIndex = 0;
            m_seenResultKeys.clear();
            m_smartSearchActive = m_smartSearchKind != SmartSearchKind::None && !m_smartQueries.empty();

            if (!m_smartSearchActive) {
                loadTargetPage(0, RequestIntent::Replace, true);
                return;
            }

            m_loadedPageOrder.clear();
            m_cachedPageOrder.clear();
            m_pageCache.clear();
            m_mergedRows.clear();
            m_totalPages = 1;
            m_totalItems = 0;
            m_currentPage = 0;
            m_requestedPage = 0;
            clearRenderedRows();
            requestNextSmartQuery();
        }

        bool queryRequiredForCurrentMode() const {
            if (m_primaryMode == PreviewPrimaryMode::Levels) {
                auto const& def = getLevelPresetDef(m_levelPreset);
                return def.usesQuery;
            }
            return true;
        }

        bool canSearchCurrentMode(std::string const& query) const {
            if (m_primaryMode == PreviewPrimaryMode::Levels) {
                auto const& def = getLevelPresetDef(m_levelPreset);
                if (!def.usesQuery) return true;
                if (def.numericOnly) return isNumericQuery(query);
                return !query.empty();
            }
            return !query.empty();
        }

        bool needsImmediateReload() const {
            return m_pageCache.empty() || m_mergedRows.empty();
        }

        std::string currentModeHint() const {
            if (m_primaryMode == PreviewPrimaryMode::Users) {
                return "Type a username";
            }
            if (m_primaryMode == PreviewPrimaryMode::Lists) {
                return "Type list name";
            }
            auto const& def = getLevelPresetDef(m_levelPreset);
            if (def.numericOnly) {
                return "Type a numeric ID";
            }
            if (def.usesQuery) {
                return "Type to search";
            }
            return "Searching...";
        }

        size_t resultsPerPage() const {
            return 10;
        }

        void clearCachedResults() {
            m_pageCache.clear();
            m_loadedPageOrder.clear();
            m_cachedPageOrder.clear();
            m_mergedRows.clear();
            m_openEntries.clear();
            m_pendingKey.clear();
            m_smartBaseQuery.clear();
            m_smartQueries.clear();
            m_seenResultKeys.clear();
            m_smartQueryIndex = 0;
            m_smartSearchActive = false;
            m_smartSearchKind = SmartSearchKind::None;
            m_pageInfoLoaded = false;
            m_totalPages = 0;
            m_totalItems = 0;
            m_itemsPerPage = static_cast<int>(resultsPerPage());
            m_currentPage = 0;
            m_requestedPage = 0;
            m_savedContentOffset = ccp(0.f, 0.f);
            m_savedContentHeight = 0.f;
            m_hasSavedContentOffset = false;
            clearRenderedRows();
        }

        void cancelPendingSearch() {
            this->unschedule(schedule_selector(RealtimeSearchBrowserPreview::firePendingSearch));
        }

        void firePendingSearch(float) {
            if (m_shuttingDown || paimon::isRuntimeShuttingDown() || !m_owner) return;
            if (!canSearchCurrentMode(m_pendingQuery)) {
                setStatus(currentModeHint().c_str());
                return;
            }

            if (m_activeQuery != m_pendingQuery) {
                clearCachedResults();
            }

            m_activeQuery = m_pendingQuery;
            m_lastRequestFailed = false;
            m_currentPage = 0;
            if (classifySmartSearch(m_pendingQuery) != SmartSearchKind::None) {
                startSmartSearch(m_pendingQuery);
            } else {
                loadTargetPage(0, RequestIntent::Replace, true);
            }
        }

        void loadTargetPage(int page, RequestIntent intent, bool force) {
            if (m_shuttingDown || paimon::isRuntimeShuttingDown()) return;
            if (m_smartSearchActive) return;
            if (!force && m_isLoading) return;
            if (page < 0) page = 0;

            m_requestedPage = page;
            captureScrollState(intent);

            if (intent == RequestIntent::Replace) {
                m_loadedPageOrder.clear();
                m_mergedRows.clear();
            }

            auto searchObject = buildSearchObjectForPage(page);
            if (!searchObject) {
                setStatus(currentModeHint().c_str());
                return;
            }

            auto key = searchObject->getKey();
            m_pendingKey = key ? key : "";
            if (m_pendingKey.empty()) {
                setStatus("Search failed");
                return;
            }

            auto manager = GameLevelManager::get();
            if (!manager) {
                setStatus("Search failed");
                return;
            }

            m_currentPage = page;
            m_isLoading = true;
            if (intent == RequestIntent::Replace || m_mergedRows.empty()) {
                setStatus("Searching...");
            } else if (m_statusLabel && m_container) {
                m_statusLabel->setString("");
                m_statusLabel->setVisible(false);
                m_container->setVisible(true);
                setQuickSearchContentVisible(false);
            }

            if (auto cached = m_pageCache.find(page); cached != m_pageCache.end() && cached->second) {
                m_isLoading = false;
                clearDelegate();
                storePage(page, cached->second);
                rebuildMergedRows();
                renderMergedRows();
                updateControlLabels();
                return;
            }

            if (auto cachedLevels = manager->getStoredOnlineLevels(searchObject->getKey())) {
                auto rows = cloneEntries(cachedLevels);
                storePage(page, rows);
                m_isLoading = false;
                clearDelegate();
                rebuildMergedRows();
                renderMergedRows();
                updateControlLabels();
                return;
            }

            manager->m_levelManagerDelegate = nullptr;
            manager->m_levelManagerDelegate = this;
            dispatchSearch(manager, searchObject);
        }

        GJSearchObject* buildSearchObjectForPage(int page) const {
            auto query = m_activeQuery.empty() ? m_pendingQuery : m_activeQuery;
            return buildSearchObjectForQuery(query, page);
        }

        void dispatchSearch(GameLevelManager* manager, GJSearchObject* object) {
            if (!manager || !object) return;
            if (m_primaryMode == PreviewPrimaryMode::Users) {
                manager->getUsers(object);
                return;
            }
            if (m_primaryMode == PreviewPrimaryMode::Lists || object->m_searchMode == 1 || object->m_searchType == SearchType::LevelListsOnClick) {
                manager->getLevelLists(object);
                return;
            }
            manager->getOnlineLevels(object);
        }

        int parsePageFromKey(std::string const& key, int fallback) const {
            auto rebuilt = GJSearchObject::createFromKey(key.c_str());
            if (!rebuilt) return fallback;
            return rebuilt->m_page;
        }

        Ref<CCArray> cloneEntries(CCArray* source) const {
            auto arr = CCArray::create();
            if (!source) return arr;
            for (auto* obj : CCArrayExt<CCObject*>(source)) {
                if (obj) arr->addObject(obj);
            }
            return arr;
        }

        void applyPageInfo(gd::string const& info) {
            auto parts = splitString(info, ':');
            if (parts.size() < 3) return;
            int totalItems = parseIntSafe(parts[0]);
            int perPage = std::max(1, parseIntSafe(parts[2], static_cast<int>(resultsPerPage())));
            m_totalItems = totalItems;
            m_itemsPerPage = perPage;
            m_totalPages = totalItems > 0 ? (totalItems + perPage - 1) / perPage : 0;
            m_pageInfoLoaded = true;
        }

        void storePage(int page, Ref<CCArray> rows) {
            m_pageCache[page] = rows;

            if (auto cachedIt = std::find(m_cachedPageOrder.begin(), m_cachedPageOrder.end(), page); cachedIt != m_cachedPageOrder.end()) {
                m_cachedPageOrder.erase(cachedIt);
            }

            m_cachedPageOrder.push_back(page);

            if (std::find(m_loadedPageOrder.begin(), m_loadedPageOrder.end(), page) == m_loadedPageOrder.end()) {
                m_loadedPageOrder.push_back(page);
                std::sort(m_loadedPageOrder.begin(), m_loadedPageOrder.end());
            }

            while (static_cast<int>(m_cachedPageOrder.size()) > kPreviewMaxCachedPages) {
                auto pageIt = std::find_if(m_cachedPageOrder.begin(), m_cachedPageOrder.end(), [this](int cachedPage) {
                    return std::find(m_loadedPageOrder.begin(), m_loadedPageOrder.end(), cachedPage) == m_loadedPageOrder.end();
                });
                if (pageIt == m_cachedPageOrder.end()) break;

                int pageToDrop = *pageIt;
                m_cachedPageOrder.erase(pageIt);
                m_pageCache.erase(pageToDrop);
            }
        }

        void captureScrollState(RequestIntent intent) {
            m_hasSavedContentOffset = false;
            m_savedContentOffset = ccp(0.f, 0.f);
            m_savedContentHeight = 0.f;

            if (intent == RequestIntent::Replace || !m_scrollLayer || !m_scrollLayer->m_contentLayer || m_mergedRows.empty()) {
                return;
            }

            auto* contentLayer = m_scrollLayer->m_contentLayer;
            m_savedContentOffset = contentLayer->getPosition();
            m_savedContentHeight = contentLayer->getContentSize().height;
            m_hasSavedContentOffset = true;
        }

        void restoreScrollAfterRender(float contentHeight) {
            if (!m_scrollLayer) return;

            if (!m_hasSavedContentOffset) {
                m_scrollLayer->scrollToTop();
            } else {
                auto targetOffset = m_savedContentOffset;
                targetOffset.y -= std::max(0.f, contentHeight - m_savedContentHeight);
                targetOffset.y = std::clamp(targetOffset.y, m_scrollLayer->getMinY(), m_scrollLayer->getMaxY());
                m_scrollLayer->setContentOffset(targetOffset, false);
                m_scrollLayer->doConstraintContent(false);
            }

            m_hasSavedContentOffset = false;
            m_savedContentOffset = ccp(0.f, 0.f);
            m_savedContentHeight = 0.f;
        }

        bool hasMorePagesToLoad() const {
            if (m_smartSearchActive) return false;
            if (m_loadedPageOrder.empty()) return false;

            int lastVisiblePage = m_loadedPageOrder.back();
            if (m_totalPages > 0) {
                return lastVisiblePage < m_totalPages - 1;
            }

            auto cached = m_pageCache.find(lastVisiblePage);
            if (cached == m_pageCache.end() || !cached->second) return false;
            return static_cast<int>(cached->second->count()) >= std::max(1, m_itemsPerPage);
        }

        void rebuildMergedRows() {
            m_mergedRows.clear();
            m_seenResultKeys.clear();
            for (int page : m_loadedPageOrder) {
                auto it = m_pageCache.find(page);
                if (it == m_pageCache.end() || !it->second) continue;
                for (auto* obj : CCArrayExt<CCObject*>(it->second)) {
                    if (obj) appendUniqueMergedObject(obj);
                }
            }
        }

        void clearRenderedRows() {
            if (m_content) {
                m_content->removeAllChildrenWithCleanup(true);
            }
            m_openEntries.clear();
        }

        void renderMergedRows() {
            if (m_shuttingDown || !m_content || !m_scrollLayer) return;
            clearRenderedRows();

            if (m_mergedRows.empty()) {
                setStatus(m_lastRequestFailed ? "Search failed" : "No results");
                updateControlLabels();
                return;
            }

            auto clipSize = m_resultsClip ? m_resultsClip->getContentSize() : CCSize{300.f, 200.f};
            auto rowH = previewRowHeightForMode(m_primaryMode);
            float totalH = 4.f + kPreviewBottomInset;
            totalH += static_cast<float>(m_mergedRows.size()) * rowH;
            if (!m_mergedRows.empty()) {
                totalH += static_cast<float>(m_mergedRows.size() - 1) * kPreviewRowGap;
            }
            totalH = std::max(totalH, clipSize.height);

            m_content->setContentSize({clipSize.width, totalH});
            m_scrollLayer->m_contentLayer->setContentSize({clipSize.width, totalH});

            float y = totalH - rowH - 2.f;
            int index = 0;
            for (auto const& entry : m_mergedRows) {
                auto rowNode = createRowNode(entry.data(), index, clipSize.width - 10.f, rowH);
                if (rowNode) {
                    rowNode->setAnchorPoint({0.f, 0.f});
                    rowNode->ignoreAnchorPointForPosition(false);
                    rowNode->setPosition({5.f, y});
                    m_content->addChild(rowNode, 1);
                }
                y -= rowH + kPreviewRowGap;
                ++index;
            }

            restoreScrollAfterRender(totalH);
            m_statusLabel->setVisible(false);
            m_container->setVisible(true);
            setQuickSearchContentVisible(false);
            updateControlLabels();
        }

        std::string currentHeaderTitle() const {
            if (m_primaryMode == PreviewPrimaryMode::Users) return "Realtime User Search";
            if (m_primaryMode == PreviewPrimaryMode::Lists) return "Realtime List Search";
            return fmt::format("Realtime {}", getLevelPresetDef(m_levelPreset).label);
        }

        void openLevel(GJGameLevel* level) {
            if (!level) return;

            releaseSearchInputFocus(m_owner);

            auto manager = GameLevelManager::get();
            auto savedLevel = manager ? manager->getSavedLevel(level->m_levelID) : nullptr;
            auto levelToUse = savedLevel ? savedLevel : level;
            if (!levelToUse) return;

            auto layer = LevelInfoLayer::create(levelToUse, false);
            auto scene = CCScene::create();
            scene->addChild(layer);
            TransitionManager::get().pushScene(scene);
        }

        void openObject(CCObject* object) {
            releaseSearchInputFocus(m_owner);

            if (auto level = typeinfo_cast<GJGameLevel*>(object)) {
                openLevel(level);
                return;
            }

            if (auto score = typeinfo_cast<GJUserScore*>(object)) {
                int accountID = score->m_accountID;
                if (accountID <= 0 && score->m_userID > 0) {
                    if (auto manager = GameLevelManager::get()) {
                        accountID = manager->accountIDForUserID(score->m_userID);
                    }
                }
                if (accountID > 0) {
                    ProfilePage::create(accountID, false)->show();
                }
                return;
            }

            if (auto list = typeinfo_cast<GJLevelList*>(object)) {
                TransitionManager::get().pushScene(LevelListLayer::scene(list));
            }
        }

        void onRowOpen(CCObject* sender) {
            auto node = typeinfo_cast<CCNode*>(sender);
            if (!node) return;

            int index = node->getTag();
            if (index < 0 || index >= static_cast<int>(m_mergedRows.size())) return;
            openObject(m_mergedRows[static_cast<size_t>(index)].data());
        }

        void addRowOpenButton(CCNode* wrapper, int index, float width, float height) {
            if (!wrapper) return;

            auto menu = CCMenu::create();
            menu->setPosition({0.f, 0.f});
            wrapper->addChild(menu, 5);

            auto hit = CCLayerColor::create({0, 0, 0, 0}, width, height);
            if (!hit) return;

            auto btn = CCMenuItemSpriteExtra::create(hit, this, menu_selector(RealtimeSearchBrowserPreview::onRowOpen));
            if (!btn) return;

            btn->setTag(index);
            btn->setPosition({width / 2.f, height / 2.f});
            menu->addChild(btn);
        }

        CCNode* createRowNode(CCObject* object, int index, float width, float height) {
            auto wrapper = CCNode::create();
            wrapper->setContentSize({width, height});
            wrapper->setAnchorPoint({0.f, 0.f});
            wrapper->ignoreAnchorPointForPosition(false);

            if (auto level = typeinfo_cast<GJGameLevel*>(object)) {
                auto cellClip = CCClippingNode::create(paimon::SpriteHelper::createRectStencil(width, height));
                if (!cellClip) return wrapper;
                cellClip->setContentSize({width, height});
                cellClip->setAnchorPoint({0.f, 0.f});
                cellClip->ignoreAnchorPointForPosition(false);
                cellClip->setPosition({0.f, 0.f});
                wrapper->addChild(cellClip, 1);

                bool oldForceCompact = paimon::hooks::g_forceCompactLevelCells;
                paimon::hooks::g_forceCompactLevelCells = true;
                auto cell = LevelCell::create(width, height);
                if (cell) {
                    cell->m_compactView = true;
                    cell->loadFromLevel(level);
                    cell->setAnchorPoint({0.f, 0.f});
                    cell->setPosition({0.f, 0.f});
                    cellClip->addChild(cell, 1);
                }
                paimon::hooks::g_forceCompactLevelCells = oldForceCompact;

                return wrapper;
            }

            auto row = PaimonDrawNode::create();
            if (row) {
                row->setPosition({0.f, 0.f});
                ccColor4F fill = (index % 2 == 0)
                    ? ccColor4F{0.10f, 0.12f, 0.17f, 0.92f}
                    : ccColor4F{0.075f, 0.085f, 0.12f, 0.92f};
                drawRect(row, {0.f, 0.f}, {width, height}, fill, 1.f, {1.f, 1.f, 1.f, 0.12f});
                drawRect(row, {0.f, 0.f}, {3.f, height}, {0.50f, 0.92f, 0.36f, 0.95f});
                wrapper->addChild(row, 0);
            }

            auto addLeftLabel = [&wrapper, width](char const* text, char const* font, float scale, CCPoint pos, ccColor3B color, float maxWidth) {
                auto label = CCLabelBMFont::create(text ? text : "", font);
                if (!label) return;
                label->setAnchorPoint({0.f, 0.5f});
                label->setScale(scale);
                label->setColor(color);
                label->setPosition(pos);
                fitLabelWidth(label, scale, maxWidth > 0.f ? maxWidth : width - pos.x - 10.f);
                wrapper->addChild(label, 2);
            };

            auto addRightLabel = [&wrapper](char const* text, char const* font, float scale, CCPoint pos, ccColor3B color, float maxWidth) {
                auto label = CCLabelBMFont::create(text ? text : "", font);
                if (!label) return;
                label->setAnchorPoint({1.f, 0.5f});
                label->setScale(scale);
                label->setColor(color);
                label->setPosition(pos);
                fitLabelWidth(label, scale, maxWidth);
                wrapper->addChild(label, 2);
            };

            if (auto level = typeinfo_cast<GJGameLevel*>(object)) {
                auto title = shorten(std::string(level->m_levelName), 26);
                auto author = shorten(std::string(level->m_creatorName), 20);
                auto meta = fmt::format("{}  {}*  {} dl  {} like",
                    difficultyName(level),
                    level->m_stars.value(),
                    formatCount(level->m_downloads),
                    formatCount(level->m_likes)
                );

                addLeftLabel(title.c_str(), "bigFont.fnt", 0.31f, {12.f, height * 0.66f}, {255, 255, 255}, width * 0.56f);
                addLeftLabel(author.c_str(), "chatFont.fnt", 0.43f, {12.f, height * 0.32f}, {170, 190, 210}, width * 0.42f);
                addRightLabel(meta.c_str(), "chatFont.fnt", 0.40f, {width - 10.f, height * 0.49f}, {215, 225, 235}, width * 0.43f);
            } else if (auto score = typeinfo_cast<GJUserScore*>(object)) {
                auto name = shorten(std::string(score->m_userName), 24);
                auto sub = fmt::format("Stars {}  Demons {}", formatCount(score->m_stars), formatCount(score->m_demons));
                auto meta = fmt::format("CP {}  UCoins {}", formatCount(score->m_creatorPoints), formatCount(score->m_userCoins));

                addLeftLabel(name.c_str(), "bigFont.fnt", 0.31f, {12.f, height * 0.64f}, {255, 255, 255}, width * 0.58f);
                addLeftLabel(sub.c_str(), "chatFont.fnt", 0.42f, {12.f, height * 0.30f}, {170, 190, 210}, width * 0.46f);
                addRightLabel(meta.c_str(), "chatFont.fnt", 0.40f, {width - 10.f, height * 0.47f}, {215, 225, 235}, width * 0.34f);
            } else if (auto list = typeinfo_cast<GJLevelList*>(object)) {
                auto title = fmt::format("List #{}", list->m_listID);
                auto sub = fmt::format("{} levels", list->m_levels.size());

                addLeftLabel(title.c_str(), "bigFont.fnt", 0.31f, {12.f, height * 0.64f}, {255, 255, 255}, width * 0.62f);
                addLeftLabel(sub.c_str(), "chatFont.fnt", 0.42f, {12.f, height * 0.30f}, {170, 190, 210}, width * 0.46f);
                addRightLabel("Open list", "chatFont.fnt", 0.40f, {width - 10.f, height * 0.47f}, {215, 225, 235}, width * 0.22f);
            }

            addRowOpenButton(wrapper, index, width, height);
            return wrapper;
        }

        void maybeAutoLoadMore() {
            if (!m_scrollLayer || !m_resultsClip || m_loadedPageOrder.empty()) return;
            if (!hasMorePagesToLoad()) return;
            auto* contentLayer = m_scrollLayer->m_contentLayer;
            if (!contentLayer) return;

            float currentOffset = contentLayer->getPositionY();
            float bottomOffset = m_scrollLayer->getMaxY();
            float distanceToBottom = std::abs(bottomOffset - currentOffset);
            if (distanceToBottom <= kPreviewAutoLoadThreshold) {
                loadTargetPage(m_loadedPageOrder.back() + 1, RequestIntent::After, false);
            }
        }

        void updateControlLabels() {
            if (m_modeLabel) {
                m_modeLabel->setVisible(false);
            }
            if (m_presetLabel) {
                m_presetLabel->setVisible(false);
            }

            setButtonText(m_modeCycleBtn, primaryModeLabel(m_primaryMode), 58.f);
            auto presetText = m_primaryMode == PreviewPrimaryMode::Levels
                ? std::string(getLevelPresetDef(m_levelPreset).label)
                : std::string(m_primaryMode == PreviewPrimaryMode::Users ? "User+1" : "Lists");
            setButtonText(m_presetCycleBtn, presetText.c_str(), 66.f);

            if (m_pageLabel) {
                auto text = m_totalPages > 0
                    ? fmt::format("{}/{}", m_currentPage + 1, m_totalPages)
                    : fmt::format("{}", m_currentPage + 1);
                m_pageLabel->setString(text.c_str());
                fitLabelWidth(m_pageLabel, 0.32f, 34.f);
                m_pageLabel->setVisible(!m_smartSearchActive);
            }
            if (m_countLabel) {
                auto text = !m_mergedRows.empty()
                    ? fmt::format("{} loaded{}", m_mergedRows.size(), m_totalItems > 0 ? fmt::format(" / {}", m_totalItems) : std::string())
                    : std::string();
                m_countLabel->setString(text.c_str());
                fitLabelWidth(m_countLabel, 0.36f, std::max(40.f, m_frameWidth - 24.f));
                m_countLabel->setVisible(!text.empty());
            }
            if (m_prevPageBtn) m_prevPageBtn->setVisible(!m_smartSearchActive);
            if (m_nextPageBtn) m_nextPageBtn->setVisible(!m_smartSearchActive);
            if (m_pageJumpBtn) m_pageJumpBtn->setVisible(!m_smartSearchActive);
        }

        void setStatus(char const* text) {
            if (!m_container || !m_statusLabel) return;
            m_statusLabel->setString(text ? text : "");
            fitLabelWidth(m_statusLabel, 0.34f, std::max(40.f, m_frameWidth - 24.f));
            m_statusLabel->setVisible(text && text[0] != '\0');
            m_container->setVisible(true);
            if (m_content) {
                clearRenderedRows();
            }
            setQuickSearchContentVisible(!(text && text[0] != '\0'));
            updateControlLabels();
        }

        void showIdle() {
            clearCachedResults();
            if (m_statusLabel) m_statusLabel->setString("");
            if (m_statusLabel) m_statusLabel->setVisible(false);
            if (m_container) m_container->setVisible(false);
            setQuickSearchContentVisible(true);
            updateControlLabels();
        }

        bool isCurrentKey(char const* key) const {
            return key && !m_pendingKey.empty() && m_pendingKey == key;
        }

        CCRect quickSearchRect() const {
            if (m_owner) {
                if (auto quickBg = m_owner->getChildByID("quick-search-bg")) {
                    return insetRect(nodeRect(quickBg), kPreviewInset, kPreviewInset);
                }
            }

            auto winSize = CCDirector::sharedDirector()->getWinSize();
            return insetRect({
                winSize.width / 2.f - kPreviewFallbackWidth / 2.f,
                winSize.height / 2.f - 8.f,
                kPreviewFallbackWidth,
                kPreviewFallbackHeight,
            }, kPreviewInset, kPreviewInset);
        }

        void setQuickSearchContentVisible(bool visible) {
            if (!m_owner) return;
            if (auto node = m_owner->getChildByID("quick-search-menu")) {
                node->setVisible(visible);
            }
        }

        void setQuickSearchTitleVisible(bool visible) {
            if (!m_owner) return;
            visitNodeTree(m_owner, [visible](CCNode* node) {
                auto label = typeinfo_cast<CCLabelBMFont*>(node);
                if (!label) return;
                auto text = std::string(label->getString());
                if (text == "QUICK SEARCH" || text == "Quick Search") {
                    label->setVisible(visible);
                }
            });
        }

        void clearDelegate() {
            auto manager = GameLevelManager::get();
            if (manager && manager->m_levelManagerDelegate == this) {
                manager->m_levelManagerDelegate = nullptr;
            }
            m_pendingKey.clear();
        }

        void onPrevMode(CCObject*) { cyclePrimaryMode(-1); }
        void onNextMode(CCObject*) { cyclePrimaryMode(1); }
        void onPrevPreset(CCObject*) { cycleLevelPreset(-1); }
        void onNextPreset(CCObject*) { cycleLevelPreset(1); }
        void onPrevPage(CCObject*) { stepPage(-1); }
        void onNextPage(CCObject*) { stepPage(1); }
        void onOpenAll(CCObject*) { openFullResults(); }

        void onPageJump(CCObject*) {
            int lastPage = std::max(1, m_totalPages > 0 ? m_totalPages : std::max(1, m_currentPage + 1));
            auto popup = SetIDPopup::create(m_currentPage + 1, 1, lastPage, "Go to page", "Go", false, 1, 60.f, false, false);
            if (!popup) return;
            popup->setTag(kPageJumpPopupTag);
            popup->m_delegate = &m_pageJumpDelegate;
            popup->show();
        }
    };
}

class $modify(MyLevelSearchLayer, LevelSearchLayer) {
    static void onModify(auto& self) {
        (void)self.setHookPriorityAfterPost("LevelSearchLayer::init", "geode.node-ids");
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

        auto onExitHook = VMTHookManager::get().addHook<
            ResolveC<MyLevelSearchLayer>::func(&MyLevelSearchLayer::onExit)
        >(this, "LevelSearchLayer::onExit");
        if (!onExitHook) {
            log::error("Failed to hook LevelSearchLayer::onExit: {}", onExitHook.unwrapErr());
        }

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
                
                if (auto preview = RealtimeSearchBrowserPreview::create(this)) {
                    this->addChild(preview, 30);
                    log::debug("[LevelSearchLayer] Recreated realtime preview in onEnter()");
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
        m_fields->m_previewCallbacksSuspended = true;
        releaseSearchInputFocus(this);

        if (auto preview = typeinfo_cast<RealtimeLevelSearchPreview*>(
            getRealtimePreviewNodeSafe()
        )) {
            preview->shutdown(true);
            preview->removeFromParentAndCleanup(true);
            return;
        }

        if (auto preview = typeinfo_cast<RealtimeSearchBrowserPreview*>(
            getRealtimePreviewNodeSafe()
        )) {
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
