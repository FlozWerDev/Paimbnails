#pragma once

#include "../services/ForumApi.hpp"
#include <Geode/Geode.hpp>
#include <Geode/ui/TextInput.hpp>
#include <cstdint>
#include <string>
#include <vector>

namespace paimon::forum {

class ForumPanel : public cocos2d::CCNodeRGBA {
public:
    enum class Scope {
        All = 0,
        Saved = 1,
        MyPosts = 2,
        Unanswered = 3,
    };

    enum class FeedSort {
        Recent = 0,
        Top = 1,
        Liked = 2,
        Discussed = 3,
    };

    static ForumPanel* create(cocos2d::CCSize size, bool gdMode);

    void onTabActivated();
    void refreshPosts(bool resetPage = false);
    void setupGlassChrome();
    bool handleScrollWheel(float x, float y);
    void filterByAuthor(int accountID, std::string const& username);
    void filterByTag(std::string const& tag);

protected:
    ~ForumPanel() override;
    bool init(cocos2d::CCSize size, bool gdMode);
    void onExit() override;
    void update(float dt) override;

private:
    bool m_gdMode = false;
    bool m_hasActivated = false;
    bool m_isLoading = false;
    bool m_lastRequestError = false;

    Scope m_scope = Scope::All;
    FeedSort m_sort = FeedSort::Recent;
    int m_page = 1;
    static constexpr int kPageSize = 25;

    std::vector<std::string> m_knownTags;
    std::vector<std::string> m_customTags;
    std::vector<std::string> m_selectedTags;
    std::string m_searchQuery;
    int m_authorFilterId = 0;
    std::string m_authorFilterName;

    bool m_searchPending = false;
    float m_searchDebounce = 0.f;
    float m_cooldownAccum = 0.f;
    bool m_wasOnCooldown = false;
    std::uint64_t m_requestSeq = 0;

    std::vector<Post> m_loadedPosts;
    std::vector<Post> m_displayedPosts;

    // Layout containers
    cocos2d::CCNodeRGBA* m_sidebarBg = nullptr;
    cocos2d::CCNodeRGBA* m_mainBg = nullptr;
    cocos2d::CCMenu* m_sidebarMenu = nullptr;
    cocos2d::CCMenu* m_mainMenu = nullptr;

    // Sidebar widgets
    CCMenuItemSpriteExtra* m_newPostBtn = nullptr;
    std::vector<CCMenuItemSpriteExtra*> m_scopeBtns;
    CCMenuItemSpriteExtra* m_clearTagsBtn = nullptr;
    geode::ScrollLayer* m_tagsScroll = nullptr;
    float m_tagsScrollTarget = 0.f;
    bool m_tagsScrollTargetSet = false;
    cocos2d::CCLabelBMFont* m_connStatusLabel = nullptr;
    cocos2d::CCLabelBMFont* m_sidebarStatsLabel = nullptr;

    // Top bar & sort widgets
    geode::TextInput* m_searchInput = nullptr;
    CCMenuItemSpriteExtra* m_clearSearchBtn = nullptr;
    CCMenuItemSpriteExtra* m_refreshBtn = nullptr;
    std::vector<CCMenuItemSpriteExtra*> m_sortBtns;
    cocos2d::CCMenu* m_activeFilterMenu = nullptr;

    // Feed area
    cocos2d::CCRect m_feedRect;
    geode::ScrollLayer* m_feedScroll = nullptr;
    geode::Scrollbar* m_feedScrollbar = nullptr;
    float m_feedScrollTarget = 0.f;
    bool m_feedScrollTargetSet = false;
    cocos2d::CCNode* m_loadingNode = nullptr;
    cocos2d::CCNode* m_emptyNode = nullptr;

    // Footer & pagination
    cocos2d::CCLabelBMFont* m_footerStatusLabel = nullptr;
    cocos2d::CCLabelBMFont* m_pageLabel = nullptr;
    CCMenuItemSpriteExtra* m_prevPageBtn = nullptr;
    CCMenuItemSpriteExtra* m_nextPageBtn = nullptr;

    // Geometry cached from init
    float m_sidebarW = 128.f;
    float m_mainX = 136.f;
    float m_mainW = 400.f;
    float m_tagsTopY = 120.f;
    float m_tagsBotY = 34.f;
    float m_sortRowY = 200.f;
    float m_sortRightEdge = 300.f;

    void buildSidebar();
    void buildMainPanel();

    void loadKnownTags();
    void mergeTagsFromPosts(std::vector<Post> const& posts);
    void rebuildTagList();
    void updateScopeButtons();
    void updateSortButtons();
    void updateActiveFiltersBar();
    void updateSidebarStats();
    void updateFooter();
    void setLoading(bool loading);

    void applyFiltersAndRender();
    void renderFeed();
    void showEmptyState();
    void clearEmptyState();
    cocos2d::CCNode* makePostCard(Post const& post, float cardW, int index);

    void openCreatePost();
    void openCreateTagPopup();
    void openPostDetail(std::string const& postId);
    void openHelpPopup();
    void toggleTagFilter(std::string const& tag);
    void toggleLikeOnFeed(std::string const& postId);
    void toggleBookmarkOnFeed(std::string const& postId);
    void clearAllFilters();
};

} // namespace paimon::forum
