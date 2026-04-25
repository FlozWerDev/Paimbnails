#pragma once
#include <Geode/Geode.hpp>
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <mutex>

namespace paimon::foryou {

class LevelTagsIntegration {
public:
    static LevelTagsIntegration& get();

    // Fetch tags for a single level. Callback receives flattened tag list.
    void fetchTagsForLevel(int levelID, std::function<void(std::vector<std::string>)> callback);

    // Fetch tags for multiple levels in batch.
    void fetchTagsForLevels(std::vector<int> const& levelIDs,
                            std::function<void(std::unordered_map<int, std::vector<std::string>>)> callback);

    // Get cached tags (empty if not fetched yet)
    std::vector<std::string> getCachedTags(int levelID) const;
    bool hasCachedTags(int levelID) const;

private:
    LevelTagsIntegration() = default;

    static constexpr char const* LEVEL_TAGS_SERVER = "https://leveltags.up.railway.app";

    std::vector<std::string> parseTags(matjson::Value const& data, int levelID) const;

    mutable std::mutex m_mutex;
    std::unordered_map<int, std::vector<std::string>> m_cache;
};

} // namespace paimon::foryou
