#pragma once

// Shop tab: reads listings and thumbs only; downloads happen in the detail popup.

#include <Geode/Geode.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/ui/TextInput.hpp>
#include "../services/CursorShopClient.hpp"

#include <array>
#include <functional>
#include <string>
#include <vector>

class CursorShopTab : public cocos2d::CCNode {
public:
    static CursorShopTab* create(cocos2d::CCSize size, std::function<void()> onInstalled);

    // Catalog loads on first tab entry; re-entry retries a failed load.
    void onShown();
    // Container popup forwards wheel and smooth-scroll ticks.
    void handleScrollWheel(float x, float y);
    void stepScroll(float dt);
    // Releases IME before the popup is destroyed.
    void shutdown();

private:
    using Store    = paimon::cursorshop::Store;
    using Category = paimon::cursorshop::Category;
    using Listing  = paimon::cursorshop::Listing;

    // One pending search-crawl request.
    struct ScanTarget {
        Category category;
        int page = 0;
    };

    std::function<void()> m_onInstalled;
    bool m_alive = true;

    Store m_store = Store::RwDesigner;
    std::array<std::vector<Category>, paimon::cursorshop::kStoreCount> m_categories{};
    std::array<int, paimon::cursorshop::kStoreCount> m_categoryIdx{};

    std::vector<Listing> m_items;
    std::vector<int> m_filtered;
    std::string m_query;

    // Tags the loaded listing so local paging never refetches.
    std::string m_loadedKey;
    int m_serverPage = 0;
    int m_serverPageCount = 1;
    int m_localPage = 0;
    // -1 jumps to the last local page after load (when paging back).
    int m_pendingLocalPage = 0;
    bool m_loading = false;

    // m_items holds search results instead of a category page.
    bool m_searchResults = false;
    // Synthetic category for the search in flight.
    Category m_searchCategory;
    bool m_scanning = false;
    std::vector<ScanTarget> m_scanTargets;
    std::size_t m_scanIndex = 0;

    geode::ScrollLayer* m_grid = nullptr;
    float m_gridScrollTargetY = 0.f;
    bool  m_gridScrollTargetSet = false;

    geode::TextInput* m_search = nullptr;
    cocos2d::CCLabelBMFont* m_categoryLabel = nullptr;
    cocos2d::CCLabelBMFont* m_pageLabel = nullptr;
    cocos2d::CCLabelBMFont* m_statusLabel = nullptr;
    cocos2d::CCLabelBMFont* m_creditLabel = nullptr;
    cocos2d::CCLabelBMFont* m_overlayLabel = nullptr;
    CCMenuItemSpriteExtra* m_overlayButton = nullptr;
    // Fires search when the local filter hit but the center overlay never showed.
    CCMenuItemSpriteExtra* m_searchButton = nullptr;
    ButtonSprite* m_overlayButtonSprite = nullptr;
    std::array<ButtonSprite*, paimon::cursorshop::kStoreCount> m_storeSprites{};

    bool initWithSize(cocos2d::CCSize size);
    void buildChrome(cocos2d::CCSize size);

    Category const& currentCategory() const;
    std::string listingKey() const;
    int storeIndex() const { return static_cast<int>(m_store); }
    // Local pages fitting one fetch.
    int localPagesPerFetch() const;
    // Local pages the loaded items span.
    int localPageCount() const;

    void selectStore(Store store);
    void applyStoreStyle();
    void ensureCategories();
    void requestPage(int serverPage);
    void fetchListing();
    bool matchesQuery(Listing const& item) const;
    void applyFilter();
    void rebuildGrid();
    void updateChrome();
    void setOverlay(std::string const& text, cocos2d::ccColor3B color);
    void setOverlayAction(char const* label, bool visible);

    void startSearch();
    void startDeepSearch();
    void stepDeepSearch();
    void finishDeepSearch();
    void stopDeepSearch();
    std::vector<ScanTarget> buildScanTargets() const;

    void onStoreButton(cocos2d::CCObject* sender);
    void onCategoryPrev(cocos2d::CCObject*);
    void onCategoryNext(cocos2d::CCObject*);
    void onPagePrev(cocos2d::CCObject*);
    void onPageNext(cocos2d::CCObject*);
    void onOverlayAction(cocos2d::CCObject*);
    void onCard(cocos2d::CCObject* sender);
};
