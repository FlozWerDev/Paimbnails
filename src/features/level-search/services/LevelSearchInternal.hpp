// Anonymous-namespace body for hooks/LevelSearchLayer.cpp; it depends on that hook's context.

#include "SearchRequestCoordinator.hpp"

namespace {
    // Keep this preview scoped to normal level search. List search uses
    // different result objects and should continue using GD's normal flow.
    bool kEnableRealtimeSearchPreview() {
        return Mod::get()->getSettingValue<bool>("realtime-search-preview");
    }
    constexpr int kRealtimeResultCount = 4;
    constexpr int kRealtimeSearchDefaultDelayMs = 450;
    constexpr int kRealtimeSearchMinDelayMs = 250;
    constexpr int kRealtimeSearchMaxDelayMs = 1000;

    // Below this length a text query matches so much that the response is
    // useless and the request is pure waste. Numeric IDs are exempt because a
    // 2-digit level ID is a legitimate exact lookup.
    constexpr std::size_t kRealtimeMinQueryChars = 3;

    // Wildcard searches expand into one request per digit. Only this many are
    // fetched up front; the rest arrive as the user scrolls.
    constexpr std::size_t kSmartInitialBatch = 2;

    // Guards the per-frame auto-load poll so a short page cannot re-request in
    // consecutive frames.
    constexpr float kAutoLoadCooldown = 0.75f;
    constexpr float kPreviewFallbackWidth = 356.f;
    constexpr float kPreviewFallbackHeight = 136.f;
    constexpr float kRealtimeRowHeight = 38.f;
    constexpr float kRealtimeRowGap = 4.f;

    float getRealtimeSearchDelay() {
        auto delayMs = std::clamp(
            Mod::get()->getSavedValue<int>("realtime-search-debounce-ms", kRealtimeSearchDefaultDelayMs),
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
        auto result = geode::utils::numFromString<int>(value);
        return result.isOk() ? result.unwrap() : fallback;
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

    // Packed integer pair: same identity as the old per-row string, no allocation.
    struct ResultKey {
        int kind = 0;
        std::int64_t id = 0;

        bool operator==(ResultKey const& other) const {
            return kind == other.kind && id == other.id;
        }
    };

    struct ResultKeyHash {
        std::size_t operator()(ResultKey const& key) const {
            return std::hash<std::int64_t>{}(key.id) ^ (static_cast<std::size_t>(key.kind) << 1);
        }
    };

    ResultKey objectUniqueKey(CCObject* object) {
        if (auto level = typeinfo_cast<GJGameLevel*>(object)) {
            return {0, static_cast<std::int64_t>(level->m_levelID.value())};
        }
        if (auto score = typeinfo_cast<GJUserScore*>(object)) {
            if (score->m_accountID > 0) return {1, static_cast<std::int64_t>(score->m_accountID)};
            if (score->m_userID > 0) return {2, static_cast<std::int64_t>(score->m_userID)};
            // Anonymous entries can only be told apart by name.
            return {3, static_cast<std::int64_t>(
                std::hash<std::string>{}(toLowerCopy(static_cast<std::string>(score->m_userName)))
            )};
        }
        if (auto list = typeinfo_cast<GJLevelList*>(object)) {
            return {4, static_cast<std::int64_t>(list->m_listID)};
        }
        return {5, static_cast<std::int64_t>(reinterpret_cast<uintptr_t>(object))};
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


    class RealtimeSearchBrowserPreview : public CCNode {    public:
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
            cancelActiveRequest();
            CCNode::cleanup();
        }

        void onExit() override {
            this->unschedule(schedule_selector(RealtimeSearchBrowserPreview::firePendingSearch));
            this->unschedule(schedule_selector(RealtimeSearchBrowserPreview::update));
            cancelActiveRequest();
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
            // scene-level popup outlives us, its delegate would dangle
            if (auto jump = m_pageJumpPopup.lock()) jump->removeFromParentAndCleanup(true);
            m_pageJumpPopup = nullptr;
            cancelPendingSearch();
            cancelActiveRequest();
            m_owner = nullptr;
            m_pendingQuery.clear();
            m_activeQuery.clear();
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
                cancelActiveRequest();
                m_activeQuery.clear();
                m_lastRequestFailed = false;
                clearCachedResults();
                showIdle();
                return;
            }

            if (!canSearchCurrentMode(m_pendingQuery)) {
                cancelActiveRequest();
                clearCachedResults();
                setStatus(currentModeHint().c_str());
                return;
            }

            // Below the minimum length nothing is dispatched, so say so instead
            // of leaving a spinner that will never resolve.
            if (queryRequiredForCurrentMode() && !queryLongEnough(m_pendingQuery)) {
                cancelActiveRequest();
                clearCachedResults();
                setStatus("Keep typing...");
                return;
            }

            if (m_pendingQuery == m_activeQuery && !m_lastRequestFailed && !needsImmediateReload()) {
                return;
            }

            // Abandon the in-flight request for the previous keystroke; its
            // result is still cached by the coordinator if it lands.
            cancelActiveRequest();
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
                cancelActiveRequest();
                m_activeQuery.clear();
                clearCachedResults();
                showIdle();
                return;
            }

            if (!canSearchCurrentMode(m_pendingQuery)) {
                cancelActiveRequest();
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
            cancelActiveRequest();
            m_needsRefreshOnEnter = true;
            TransitionManager::get().pushScene(LevelBrowserLayer::scene(object));
        }

        // The preview no longer implements LevelManagerDelegate; the coordinator
        // owns the delegate slot and hands results back through callbacks.

        void update(float dt) override {
            if (m_shuttingDown || !m_scrollLayer || !m_scrollLayer->m_contentLayer || !m_content) return;
            if (m_autoLoadCooldown > 0.f) {
                m_autoLoadCooldown = std::max(0.f, m_autoLoadCooldown - dt);
            }
            if (m_smartSearchActive && !m_isLoading && m_smartQueryIndex < m_smartQueries.size()) {
                // Remaining wildcard variants are only fetched once the user
                // scrolls to the end of what we already have.
                if (isNearBottom()) {
                    m_smartDispatched = 0;
                    requestNextSmartQuery();
                }
                return;
            }
            if (!m_isLoading) {
                maybeAutoLoadMore();
            }
        }

        bool isNearBottom() const {
            if (!m_scrollLayer || !m_scrollLayer->m_contentLayer) return false;
            float currentOffset = m_scrollLayer->m_contentLayer->getPositionY();
            float bottomOffset = m_scrollLayer->getMaxY();
            return std::abs(bottomOffset - currentOffset) <= kPreviewAutoLoadThreshold;
        }

    private:
        enum class SmartSearchKind {
            None,
            LevelExact,
            LevelWildcard,
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
        CCMenuItemSpriteExtra* m_prevPageBtn = nullptr;
        CCMenuItemSpriteExtra* m_nextPageBtn = nullptr;
        CCMenuItemSpriteExtra* m_pageJumpBtn = nullptr;
        CCMenuItemSpriteExtra* m_openAllBtn = nullptr;
        CCMenuItemSpriteExtra* m_modeCycleBtn = nullptr;
        CCMenuItemSpriteExtra* m_presetCycleBtn = nullptr;
        RealtimePageJumpDelegate m_pageJumpDelegate;
        WeakRef<SetIDPopup> m_pageJumpPopup;
        PreviewPrimaryMode m_primaryMode = PreviewPrimaryMode::Levels;
        LevelPreset m_levelPreset = LevelPreset::Search;
        std::string m_pendingQuery;
        std::string m_activeQuery;
        std::string m_smartBaseQuery;
        std::unordered_map<int, Ref<CCArray>> m_pageCache;
        std::vector<int> m_loadedPageOrder;
        std::vector<int> m_cachedPageOrder;
        std::vector<Ref<CCObject>> m_mergedRows;
        std::vector<std::string> m_smartQueries;
        std::unordered_set<ResultKey, ResultKeyHash> m_seenResultKeys;
        paimon::levelsearch::SearchRequestCoordinator::Token m_activeRequest = 0;
        size_t m_smartQueryIndex = 0;
        size_t m_smartDispatched = 0;
        bool m_smartLoadAll = false;
        CCSprite* m_loadingIndicator = nullptr;
        CCLabelBMFont* m_quickSearchTitle = nullptr;
        float m_autoLoadCooldown = 0.f;
        int m_lastAutoLoadedPage = -1;
        int m_currentPage = 0;
        int m_requestedPage = 0;
        int m_totalPages = 0;
        int m_totalItems = 0;
        int m_itemsPerPage = kRealtimeResultCount;
        CCPoint m_savedContentOffset = {0.f, 0.f};
        float m_savedContentHeight = 0.f;
        std::size_t m_renderedRowCount = 0;
        float m_renderedTotalHeight = 0.f;
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

            // These four values define the level row geometry and are deliberately
            // unchanged by the redesign: the list keeps its current size.
            auto resultsBottomY = -frameH / 2.f + 4.f;
            auto resultsHeight = std::max(64.f, frameH - 8.f);
            auto countY = resultsBottomY + 7.f;
            auto resultsCenterY = resultsBottomY + resultsHeight / 2.f;

            // Vanilla GD panel instead of a hand-drawn rounded rect, so the
            // preview matches the surrounding search UI.
            if (auto* panel = paimon::SpriteHelper::safeCreateScale9(
                "GJ_square05.png", CCRectMake(14.f, 14.f, 12.f, 12.f)
            )) {
                panel->setContentSize({frameW - 12.f, resultsHeight});
                panel->setAnchorPoint({0.f, 0.f});
                panel->setPosition({-(frameW - 12.f) / 2.f, resultsBottomY});
                m_container->addChild(panel, 1);
            } else if (auto* fallback = paimon::SpriteHelper::createRoundedRect(
                frameW - 12.f,
                resultsHeight,
                6.f,
                {0.01f, 0.015f, 0.03f, 0.84f},
                {1.f, 1.f, 1.f, 0.18f},
                0.8f
            )) {
                fallback->setPosition({-(frameW - 12.f) / 2.f, resultsBottomY});
                m_container->addChild(fallback, 1);
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

            m_pageLabel = CCLabelBMFont::create("", "goldFont.fnt");
            m_pageLabel->setScale(0.30f);
            m_pageLabel->setAnchorPoint({0.5f, 0.5f});
            m_pageLabel->setPosition({rightPageX, headerY});
            m_container->addChild(m_pageLabel, 4);

            m_prevPageBtn = makeArrowButton(false, menu_selector(RealtimeSearchBrowserPreview::onPrevPage));
            m_nextPageBtn = makeArrowButton(true, menu_selector(RealtimeSearchBrowserPreview::onNextPage));
            if (m_prevPageBtn) {
                m_prevPageBtn->setPosition({rightPrevX, headerY});
                m_controlsMenu->addChild(m_prevPageBtn);
            }
            if (m_nextPageBtn) {
                m_nextPageBtn->setPosition({rightNextX, headerY});
                m_controlsMenu->addChild(m_nextPageBtn);
            }

            auto pageSpr = ButtonSprite::create("Go", 36, true, "bigFont.fnt", "GJ_button_02.png", 18.f, 0.45f);
            if (pageSpr) {
                m_pageJumpBtn = CCMenuItemSpriteExtra::create(pageSpr, this, menu_selector(RealtimeSearchBrowserPreview::onPageJump));
                m_pageJumpBtn->setPosition({rightGoX, headerY});
                m_controlsMenu->addChild(m_pageJumpBtn);
            }

            // The magnifier reads as "see all results" without needing a label.
            if (auto* findIcon = paimon::SpriteHelper::safeCreateWithFrameName("gj_findBtn_001.png")) {
                findIcon->setScale(0.65f);
                m_openAllBtn = CCMenuItemSpriteExtra::create(findIcon, this, menu_selector(RealtimeSearchBrowserPreview::onOpenAll));
            } else if (auto openSpr = ButtonSprite::create("All", 38, true, "bigFont.fnt", "GJ_button_01.png", 18.f, 0.40f)) {
                m_openAllBtn = CCMenuItemSpriteExtra::create(openSpr, this, menu_selector(RealtimeSearchBrowserPreview::onOpenAll));
            }
            if (m_openAllBtn) {
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

            // Spinner for background page loads, so appending a page no longer
            // needs to blank the list to show progress.
            if (auto* spinner = paimon::SpriteHelper::safeCreate("loadingCircle.png")) {
                spinner->setScale(0.38f);
                spinner->setPosition({0.f, resultsCenterY});
                spinner->setVisible(false);
                spinner->setBlendFunc({GL_SRC_ALPHA, GL_ONE});
                m_container->addChild(spinner, 5);
                m_loadingIndicator = spinner;
            }

            // Unchanged: same clip size, so level rows keep their current width
            // and the 45px cell height still fits exactly as before.
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

        // Vanilla GD pager arrow rather than a "<"/">" ButtonSprite.
        CCMenuItemSpriteExtra* makeArrowButton(bool forward, SEL_MenuHandler handler) {
            if (auto* arrow = paimon::SpriteHelper::safeCreateWithFrameName("GJ_arrow_03_001.png")) {
                arrow->setScale(0.55f);
                if (forward) arrow->setFlipX(true);
                auto btn = CCMenuItemSpriteExtra::create(arrow, this, handler);
                if (btn) btn->setSizeMult(1.2f);
                return btn;
            }

            auto spr = ButtonSprite::create(forward ? ">" : "<", 30, true, "bigFont.fnt", "GJ_button_02.png", 18.f, 0.40f);
            if (!spr) return nullptr;
            auto btn = CCMenuItemSpriteExtra::create(spr, this, handler);
            if (btn) btn->setSizeMult(1.0f);
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

        // Reuse the label inside the existing sprite: rebuilding it per response is wasteful.
        void setButtonText(CCMenuItemSpriteExtra* btn, char const* text, float width) {
            if (!btn || !text) return;

            if (auto* sprite = typeinfo_cast<ButtonSprite*>(btn->getNormalImage())) {
                if (sprite->m_label) {
                    if (std::string(sprite->m_label->getString()) == text) return;
                    sprite->setString(text);
                    return;
                }
            }

            auto makeSprite = [text, width]() -> ButtonSprite* {
                return ButtonSprite::create(text, static_cast<int>(width), true, "bigFont.fnt", "GJ_button_02.png", 18.f, 0.34f);
            };
            if (auto normal = makeSprite()) btn->setNormalImage(normal);
            if (auto selected = makeSprite()) btn->setSelectedImage(selected);
            if (auto disabled = makeSprite()) btn->setDisabledImage(disabled);
        }

        SmartSearchKind classifySmartSearch(std::string const& query) const {
            if (query.empty()) return SmartSearchKind::None;

            // GD's user search already prefix-matches: one request plus client-side ranking replaces the old 37-request fan-out.
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

                case SmartSearchKind::None:
                default:
                    break;
            }
            return queries;
        }

        float currentDebounceDelay() const {
            // The wildcard path issues several requests, so give the user longer
            // to finish typing before committing to it.
            return classifySmartSearch(m_pendingQuery) == SmartSearchKind::LevelWildcard
                ? std::max(0.8f, getRealtimeSearchDelay())
                : getRealtimeSearchDelay();
        }

        // A text query has to be long enough to be worth a round trip. Numeric
        // IDs bypass this because they are exact lookups.
        bool queryLongEnough(std::string const& query) const {
            if (query.empty()) return false;
            if (isNumericQuery(query) || isDigitWildcardQuery(query)) return true;
            return query.size() >= kRealtimeMinQueryChars;
        }

        paimon::levelsearch::SearchKind coordinatorKind() const {
            switch (m_primaryMode) {
                case PreviewPrimaryMode::Users: return paimon::levelsearch::SearchKind::Users;
                case PreviewPrimaryMode::Lists: return paimon::levelsearch::SearchKind::Lists;
                case PreviewPrimaryMode::Levels:
                default: return paimon::levelsearch::SearchKind::Levels;
            }
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
            cancelActiveRequest();
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

            // Only fetch a bounded batch up front; the rest is pulled in by
            // maybeAutoLoadMore() when the user actually scrolls that far.
            if (m_smartQueryIndex >= m_smartQueries.size() ||
                (!m_smartLoadAll && m_smartDispatched >= kSmartInitialBatch)) {
                finalizeSmartSearch();
                return;
            }

            auto query = m_smartQueries[m_smartQueryIndex++];
            auto object = buildSearchObjectForQuery(query, 0);
            if (!object) {
                requestNextSmartQuery();
                return;
            }

            ++m_smartDispatched;
            m_isLoading = true;
            cancelActiveRequest();
            m_activeRequest = paimon::levelsearch::SearchRequestCoordinator::get().request(
                coordinatorKind(),
                object,
                [this](bool ok, CCArray* items, std::string const&) {
                    if (m_shuttingDown || paimon::isRuntimeShuttingDown()) return;

                    m_activeRequest = 0;
                    m_isLoading = false;

                    if (ok && items) {
                        for (auto* entry : CCArrayExt<CCObject*>(items)) {
                            appendUniqueMergedObject(entry);
                        }
                        renderPartialSmartResults();
                    }

                    requestNextSmartQuery();
                }
            );
        }

        void startSmartSearch(std::string const& query) {
            m_smartBaseQuery = query;
            m_smartSearchKind = classifySmartSearch(query);
            m_smartQueries = buildSmartQueries(query);
            m_smartQueryIndex = 0;
            m_smartDispatched = 0;
            m_smartLoadAll = m_smartSearchKind == SmartSearchKind::LevelExact;
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
            cancelActiveRequest();
            m_pageCache.clear();
            m_loadedPageOrder.clear();
            m_cachedPageOrder.clear();
            m_mergedRows.clear();
            m_smartBaseQuery.clear();
            m_smartQueries.clear();
            m_seenResultKeys.clear();
            m_smartQueryIndex = 0;
            m_smartDispatched = 0;
            m_smartLoadAll = false;
            m_smartSearchActive = false;
            m_smartSearchKind = SmartSearchKind::None;
            m_pageInfoLoaded = false;
            m_totalPages = 0;
            m_totalItems = 0;
            m_itemsPerPage = static_cast<int>(resultsPerPage());
            m_currentPage = 0;
            m_requestedPage = 0;
            m_lastAutoLoadedPage = -1;
            m_autoLoadCooldown = 0.f;
            m_isLoading = false;
            setLoadingIndicatorVisible(false);
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

            // Too short to be worth a round trip.
            if (queryRequiredForCurrentMode() && !queryLongEnough(m_pendingQuery)) {
                clearCachedResults();
                setStatus("Keep typing...");
                return;
            }

            // If a prefix of this query already came back empty, this one
            // cannot match anything either, so answer locally.
            auto& coordinator = paimon::levelsearch::SearchRequestCoordinator::get();
            if (queryRequiredForCurrentMode() &&
                coordinator.isKnownEmpty(coordinatorKind(), m_pendingQuery)) {
                clearCachedResults();
                m_activeQuery = m_pendingQuery;
                setStatus("No results");
                return;
            }

            if (m_activeQuery != m_pendingQuery) {
                clearCachedResults();
            }

            m_activeQuery = m_pendingQuery;
            m_lastRequestFailed = false;
            m_currentPage = 0;
            m_lastAutoLoadedPage = -1;
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

            // Local page cache first: paging back is free.
            if (auto cached = m_pageCache.find(page); cached != m_pageCache.end() && cached->second) {
                m_isLoading = false;
                storePage(page, cached->second);
                m_currentPage = page;
                rebuildMergedRows();
                renderMergedRows();
                updateControlLabels();
                return;
            }

            m_currentPage = page;
            m_isLoading = true;
            if (intent == RequestIntent::Replace || m_mergedRows.empty()) {
                setStatus("Searching...");
            } else {
                hideStatus();
                setLoadingIndicatorVisible(true);
            }

            requestPage(page, searchObject);
        }

        // Single entry point to the network. Everything is funnelled through the
        // coordinator so the cache, de-duplication and rate limit apply
        // uniformly, and so m_levelManagerDelegate is restored afterwards.
        void requestPage(int page, GJSearchObject* object) {
            cancelActiveRequest();

            auto kind = coordinatorKind();
            m_activeRequest = paimon::levelsearch::SearchRequestCoordinator::get().request(
                kind,
                object,
                [this, page](bool ok, CCArray* items, std::string const& pageInfo) {
                    if (m_shuttingDown || paimon::isRuntimeShuttingDown()) return;

                    m_activeRequest = 0;
                    m_isLoading = false;
                    setLoadingIndicatorVisible(false);

                    if (!ok) {
                        m_lastRequestFailed = true;
                        if (m_mergedRows.empty()) {
                            setStatus("Search failed");
                        } else {
                            updateControlLabels();
                        }
                        return;
                    }

                    m_lastRequestFailed = false;
                    if (!pageInfo.empty()) {
                        applyPageInfo(pageInfo);
                    }

                    m_currentPage = page;
                    storePage(page, cloneEntries(items));
                    rebuildMergedRows();
                    renderMergedRows();
                    updateControlLabels();
                }
            );
        }

        void cancelActiveRequest() {
            if (m_activeRequest != 0) {
                paimon::levelsearch::SearchRequestCoordinator::get().cancel(m_activeRequest);
                m_activeRequest = 0;
            }
        }

        GJSearchObject* buildSearchObjectForPage(int page) const {
            auto query = m_activeQuery.empty() ? m_pendingQuery : m_activeQuery;
            return buildSearchObjectForQuery(query, page);
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

            // The old eviction loop never dropped anything (infinite scroll marks every
            // page loaded), so evict by age and keep the merged list consistent.
            while (static_cast<int>(m_cachedPageOrder.size()) > kPreviewMaxCachedPages) {
                int pageToDrop = m_cachedPageOrder.front();
                if (pageToDrop == page) break;

                m_cachedPageOrder.erase(m_cachedPageOrder.begin());
                m_pageCache.erase(pageToDrop);
                m_loadedPageOrder.erase(
                    std::remove(m_loadedPageOrder.begin(), m_loadedPageOrder.end(), pageToDrop),
                    m_loadedPageOrder.end()
                );
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

        void clearRenderedRows() {
            if (m_content) {
                m_content->removeAllChildrenWithCleanup(true);
            }
            m_renderedRowCount = 0;
            m_renderedTotalHeight = 0.f;
        }

        // Merged rows are append-only within a query, so a rebuild that only
        // adds to the tail can reuse the nodes already rendered.
        void rebuildMergedRows() {
            std::size_t previous = m_mergedRows.size();
            m_mergedRows.clear();
            m_seenResultKeys.clear();
            for (int page : m_loadedPageOrder) {
                auto it = m_pageCache.find(page);
                if (it == m_pageCache.end() || !it->second) continue;
                for (auto* obj : CCArrayExt<CCObject*>(it->second)) {
                    if (obj) appendUniqueMergedObject(obj);
                }
            }

            // Eviction can shrink the merged list; rendered nodes no longer
            // line up with it, so start over in that case.
            if (m_mergedRows.size() < previous) {
                clearRenderedRows();
            }
        }

        void renderMergedRows() {
            if (m_shuttingDown || !m_content || !m_scrollLayer) return;

            if (m_mergedRows.empty()) {
                clearRenderedRows();
                setStatus(m_lastRequestFailed ? "Search failed" : "No results");
                updateControlLabels();
                return;
            }

            auto clipSize = m_resultsClip ? m_resultsClip->getContentSize() : CCSize{300.f, 200.f};
            auto rowH = previewRowHeightForMode(m_primaryMode);

            // Rows are immutable once built: keep rendered ones, only create the new tail.
            std::size_t existing = m_renderedRowCount;
            if (existing > m_mergedRows.size()) {
                clearRenderedRows();
                existing = 0;
            }

            float totalH = 4.f + kPreviewBottomInset;
            totalH += static_cast<float>(m_mergedRows.size()) * rowH;
            totalH += static_cast<float>(m_mergedRows.size() - 1) * kPreviewRowGap;
            totalH = std::max(totalH, clipSize.height);

            m_content->setContentSize({clipSize.width, totalH});
            m_scrollLayer->m_contentLayer->setContentSize({clipSize.width, totalH});

            // Existing rows are positioned relative to the old total height, so
            // shift them by the delta rather than rebuilding them.
            if (existing > 0 && m_renderedTotalHeight > 0.f && totalH != m_renderedTotalHeight) {
                float shift = totalH - m_renderedTotalHeight;
                for (auto* child : CCArrayExt<CCNode*>(m_content->getChildren())) {
                    child->setPositionY(child->getPositionY() + shift);
                }
            }

            float y = totalH - rowH - 2.f - static_cast<float>(existing) * (rowH + kPreviewRowGap);
            for (std::size_t index = existing; index < m_mergedRows.size(); ++index) {
                auto rowNode = createRowNode(m_mergedRows[index].data(), static_cast<int>(index), clipSize.width - 10.f, rowH);
                if (rowNode) {
                    rowNode->setAnchorPoint({0.f, 0.f});
                    rowNode->ignoreAnchorPointForPosition(false);
                    rowNode->setPosition({5.f, y});
                    m_content->addChild(rowNode, 1);
                }
                y -= rowH + kPreviewRowGap;
            }

            m_renderedRowCount = m_mergedRows.size();
            m_renderedTotalHeight = totalH;

            restoreScrollAfterRender(totalH);
            hideStatus();
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

            auto manager = GameLevelManager::get();
            auto savedLevel = manager ? manager->getSavedLevel(level->m_levelID) : nullptr;
            auto levelToUse = savedLevel ? savedLevel : level;
            if (!levelToUse) return;

            auto layer = LevelInfoLayer::create(levelToUse, false);
            auto scene = CCScene::create();
            scene->addChild(layer);

            // Release IME focus and freeze callbacks before changing scenes;
            // defer node removal until the touch handler unwinds.
            prepareForSceneTransition();

            TransitionManager::get().pushScene(scene);
        }

        void openObject(CCObject* object) {
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
                prepareForSceneTransition();
                if (accountID > 0) {
                    ProfilePage::create(accountID, false)->show();
                }
                return;
            }

            if (auto list = typeinfo_cast<GJLevelList*>(object)) {
                auto scene = LevelListLayer::scene(list);
                prepareForSceneTransition();
                TransitionManager::get().pushScene(scene);
            }
        }

        // Detach IME and freeze callbacks without removing nodes from a touch handler.
        void prepareForSceneTransition() {
            // Release IME focus so the next scene cannot receive keys through this input.
            releaseSearchInputFocus(m_owner);

            m_shuttingDown = true;
            cancelPendingSearch();
            cancelActiveRequest();
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

        // BoundedTouchMenu rejects touches outside the visible clip rect;
        // CCClippingNode only clips rendering.
            auto menu = BoundedTouchMenu::create();
            menu->setBoundsNode(m_resultsClip);
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
                auto cellClip = paimon::ScissorClipNode::create(paimon::SpriteHelper::createRectStencil(width, height));
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
            if (m_autoLoadCooldown > 0.f) return;
            if (!hasMorePagesToLoad()) return;
            auto* contentLayer = m_scrollLayer->m_contentLayer;
            if (!contentLayer) return;

            float currentOffset = contentLayer->getPositionY();
            float bottomOffset = m_scrollLayer->getMaxY();
            float distanceToBottom = std::abs(bottomOffset - currentOffset);
            if (distanceToBottom > kPreviewAutoLoadThreshold) return;

            int nextPage = m_loadedPageOrder.back() + 1;

            // Without this, a page that comes back shorter than expected keeps
            // matching the scroll condition and re-requests every frame.
            if (nextPage == m_lastAutoLoadedPage) return;

            m_lastAutoLoadedPage = nextPage;
            m_autoLoadCooldown = kAutoLoadCooldown;
            loadTargetPage(nextPage, RequestIntent::After, false);
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
            setLoadingIndicatorVisible(false);
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

        // Clears the status without wiping rendered rows. setStatus() tears the
        // list down, which is wrong when results are already on screen.
        void hideStatus() {
            if (m_statusLabel) {
                m_statusLabel->setString("");
                m_statusLabel->setVisible(false);
            }
            if (m_container) {
                m_container->setVisible(true);
            }
        }

        void setLoadingIndicatorVisible(bool visible) {
            if (!m_loadingIndicator) return;
            m_loadingIndicator->setVisible(visible);
            if (visible) {
                m_loadingIndicator->runAction(CCRepeatForever::create(
                    CCRotateBy::create(1.f, 360.f)
                ));
            } else {
                m_loadingIndicator->stopAllActions();
            }
        }

        void showIdle() {
            clearCachedResults();
            if (m_statusLabel) m_statusLabel->setString("");
            if (m_statusLabel) m_statusLabel->setVisible(false);
            if (m_container) m_container->setVisible(false);
            setQuickSearchContentVisible(true);
            updateControlLabels();
        }

        CCRect quickSearchRect() const {
            if (m_owner) {
                if (auto quickBg = m_owner->getChildByID("quick-search-bg")) {
                    return insetRect(nodeRect(quickBg), kPreviewInset, kPreviewInset);
                }
            }

            auto winSize = CCDirector::get()->getWinSize();
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

            // Resolved once: the walk compares the text of every label in the
            // layer, so repeating it on each state change is wasteful.
            if (!m_quickSearchTitle) {
                visitNodeTree(m_owner, [this](CCNode* node) {
                    if (m_quickSearchTitle) return;
                    auto label = typeinfo_cast<CCLabelBMFont*>(node);
                    if (!label) return;
                    auto text = std::string(label->getString());
                    if (text == "QUICK SEARCH" || text == "Quick Search") {
                        m_quickSearchTitle = label;
                    }
                });
            }

            if (m_quickSearchTitle) {
                m_quickSearchTitle->setVisible(visible);
            }
        }

        void onNextMode(CCObject*) { cyclePrimaryMode(1); }
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
            m_pageJumpPopup = popup;
            popup->show();
        }
    };
}
