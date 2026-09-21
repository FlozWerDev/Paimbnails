#pragma once

// Everything the Info Suite remembers between sessions (info_suite.json).
// Per-level progress lives in ProgressTracker: written far more often.

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace paimon::info {

class InfoStore {
public:
    static InfoStore& get();

    // Keyed by a page independent digest of the search filters.
    std::optional<int> lastPage(std::string const& searchKey) const;
    void setLastPage(std::string const& searchKey, int page);

    std::string username(int userID) const;
    void rememberUsername(int userID, std::string const& name);

    std::string levelDate(int levelID) const;
    void setLevelDate(int levelID, std::string const& isoDate);

    void addCommentSample(int64_t commentID, int64_t epochSeconds);
    // Estimated epoch seconds for a comment id, or 0 when there is not
    // enough data on both sides of it yet.
    int64_t estimateCommentTime(int64_t commentID) const;

    void save();

private:
    InfoStore() { load(); }
    void load();

    std::unordered_map<std::string, int> m_lastPages;
    std::unordered_map<int, std::string> m_usernames;
    std::unordered_map<int, std::string> m_levelDates;
    std::vector<std::pair<int64_t, int64_t>> m_commentSamples;  // sorted by id

    bool m_dirty = false;
};

} // namespace paimon::info
