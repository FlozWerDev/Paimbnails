#pragma once

// Client for kampwski.level_tags. Uses /tags, /get, and /search; the base URL
// follows Level Tags' setting with the published server as fallback.

#include <Geode/Geode.hpp>
#include <matjson.hpp>
#include <cocos2d.h>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace paimon::foryou {

enum class TagCategory {
    Style,
    Theme,
    Meta,
    Gameplay,
    Unknown
};

char const* tagCategoryKey(TagCategory category);
TagCategory tagCategoryFromKey(std::string_view key);

struct TagInfo {
    std::string name;
    TagCategory category = TagCategory::Unknown;
    cocos2d::ccColor3B color{255, 255, 255};
    std::string description;
};

using LevelTagMap = std::unordered_map<int, std::vector<std::string>>;

class LevelTagsClient {
public:
    static LevelTagsClient& get();

    // True only when Level Tags is installed and enabled.
    static bool isAvailable();
    // Configured server URL or the published default.
    static std::string serverURL();

    // Fetch once per session; cached calls invoke the callback immediately.
    void loadCatalog(std::function<void(bool ok)> callback = nullptr);
    bool hasCatalog() const;
    std::vector<TagInfo> catalog() const;
    std::vector<TagInfo> catalogFor(TagCategory category) const;
    TagCategory categoryOf(std::string const& tag) const;
    std::optional<TagInfo> infoFor(std::string const& tag) const;

    // Batch uncached IDs; cached tags are answered from memory.
    void fetchTags(std::vector<int> const& levelIDs, std::function<void(LevelTagMap)> callback);
    std::vector<std::string> cachedTags(int levelID) const;
    bool isResolved(int levelID) const;

    // Find IDs with every include tag and none of the excludes.
    void searchByTags(std::vector<std::string> const& include,
                      std::vector<std::string> const& exclude,
                      std::function<void(std::vector<int>)> callback);

    void loadDiskCache();
    void saveDiskCache();

private:
    LevelTagsClient() = default;

    static std::string urlEncode(std::string const& value);

    void parseCatalog(matjson::Value const& root);
    LevelTagMap parseLevelTags(matjson::Value const& root) const;

    std::filesystem::path cachePath() const;

    // Bound /get URL length.
    static constexpr size_t kFetchChunkSize = 50;
    static constexpr char const* kDefaultServer = "https://leveltags.up.railway.app";
    static constexpr char const* kModID = "kampwski.level_tags";

    mutable std::mutex m_mutex;
    std::unordered_map<std::string, TagInfo> m_catalog;
    std::unordered_map<int, std::vector<std::string>> m_levelTags;
    bool m_catalogLoaded = false;
    bool m_catalogPending = false;
    bool m_diskCacheLoaded = false;
    bool m_dirty = false;
    std::vector<std::function<void(bool)>> m_catalogWaiters;
};

}
