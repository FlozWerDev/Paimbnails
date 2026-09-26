#pragma once
// Cursor shop: only installed items are downloaded, never bulk-crawled.
// All callbacks return on main thread.

#include <Geode/Geode.hpp>
#include "CursorManager.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace paimon::cursorshop {

enum class Store {
    RwDesigner   = 0,
    CustomCursor = 1,
};

inline constexpr int kStoreCount = 2;

struct Category {
    std::string id;
    std::string name;
    // Paged sections fetch per page; the rest arrives whole and paginates locally.
    bool paged = false;
    // Listings per request; rw-designer rounds offsets to it. 0 = whole catalog.
    int fetchSize = 0;
};

// Grid entry: an rw-designer set, a junkyard single, or a custom-cursor pack.
struct Listing {
    Store store = Store::RwDesigner;
    std::string id;
    std::string name;
    std::string author;
    std::string extra;      // descargas, numero de cursores...
    std::string thumbUrl;
    // Singles already know their file; no detail page needed.
    std::string directUrl;
    bool single = false;
    // .ani converts to GIF on install.
    bool animated = false;
};

struct ListingPage {
    std::vector<Listing> items;
    int page = 0;
    int pageCount = 1;
};

struct DetailCursor {
    std::string name;
    std::string previewUrl;
    std::string downloadUrl;
    // Some packs ship two sizes; try large, fall back to page size.
    std::string fallbackUrl;
    // Both stores tag cursor roles, so a state can usually be suggested.
    CursorState suggested = CursorState::Idle;
    bool hasSuggested = false;
    bool animated = false;
};

struct Detail {
    std::string name;
    std::string author;
    std::string description;
    // Canonical page (empty when unknown); UI links it so authors get the visit.
    std::string sourceUrl;
    std::vector<DetailCursor> cursors;
};

class ShopClient final {
public:
    using ListingCallback  = geode::CopyableFunction<void(geode::Result<ListingPage>)>;
    using DetailCallback   = geode::CopyableFunction<void(geode::Result<Detail>)>;
    using BytesCallback    = geode::CopyableFunction<void(geode::Result<std::vector<std::uint8_t>>)>;
    using CategoryCallback = geode::CopyableFunction<void(std::vector<Category>)>;

    static char const* storeName(Store store);
    static char const* storeCredit(Store store);

    static std::vector<Category> builtinCategories(Store store);

    // rw-designer has its own search; custom-cursor's is blocked, walk collections.
    static bool supportsSearch(Store store);
    // Synthetic category that fetchListing resolves as a search.
    static Category searchCategory(Store store, std::string const& query);
    // Adds custom-cursor recents to the fixed ones.
    static void fetchCategories(Store store, CategoryCallback cb);

    static void fetchListing(Store store, Category const& category, int page, ListingCallback cb);
    static void fetchDetail(Listing const& listing, DetailCallback cb);

    // Only from explicit user action.
    static void download(std::string const& url, BytesCallback cb);
    static void download(std::string const& url, std::string const& fallbackUrl, BytesCallback cb);

    static std::string filenameFor(std::string const& url, std::string const& fallbackStem);

private:
    ShopClient() = delete;
};

} // namespace paimon::cursorshop
