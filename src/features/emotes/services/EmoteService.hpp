#pragma once

#include <Geode/Geode.hpp>
#include <Geode/utils/function.hpp>
#include "../models/EmoteModels.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <optional>
#include <atomic>

namespace paimon::emotes {

// Singleton service that fetches the emote catalog from the server
// and provides lookup by name. Thread-safe when accessed via snapshot-returning APIs.
class EmoteService {
public:
    static EmoteService& get() {
        static EmoteService instance;
        return instance;
    }

    using CatalogCallback = geode::CopyableFunction<void(bool success)>;

    // Fetch the full emote catalog from the API (all pages).
    // Calls back on main thread when done.
    void fetchAllEmotes(CatalogCallback callback = nullptr);

    // Get an emote by name from the in-memory catalog.
    std::optional<EmoteInfo> getEmoteByName(std::string const& name) const;

    // Search emotes whose names start with (or contain) the query.
    // Returns up to maxResults matches.
    std::vector<EmoteInfo> searchEmotes(std::string const& query, size_t maxResults = 5) const;

    // All emotes split by type
    std::vector<EmoteInfo> getGifEmotes() const {
        std::lock_guard lock(m_mutex);
        return m_gifEmotes;
    }
    std::vector<EmoteInfo> getStaticEmotes() const {
        std::lock_guard lock(m_mutex);
        return m_staticEmotes;
    }
    std::vector<EmoteInfo> getAllEmotes() const {
        std::lock_guard lock(m_mutex);
        return m_allEmotes;
    }

    // Category helpers (filter from in-memory vectors)
    std::vector<std::string> getCategories(EmoteType type) const;
    std::vector<std::string> getAllCategories() const;
    std::vector<EmoteInfo> getEmotesByCategory(EmoteType type, std::string const& category) const;
    std::vector<EmoteInfo> getAllEmotesByCategory(std::string const& category) const;
    std::optional<EmoteInfo> getRandomEmote() const;

    bool isLoaded() const { return m_loaded.load(std::memory_order_acquire); }
    bool isFetching() const { return m_fetching.load(std::memory_order_acquire); }

    // Persist catalog to disk (mod save dir) and load on startup
    void saveCatalogToDisk();
    void loadCatalogFromDisk();

    // Clear everything (called from PaiConfiguracion cache clear)
    void clearCatalog();

private:
    EmoteService() = default;
    ~EmoteService() = default;
    EmoteService(EmoteService const&) = delete;
    EmoteService& operator=(EmoteService const&) = delete;

    void fetchPage(int page, int limit, std::string const& timelast,
                   std::vector<EmoteInfo>& accumulator, CatalogCallback callback);
    void buildIndex();

    mutable std::mutex m_mutex;
    std::vector<EmoteInfo> m_allEmotes;
    std::vector<EmoteInfo> m_gifEmotes;
    std::vector<EmoteInfo> m_staticEmotes;
    std::unordered_map<std::string, size_t> m_nameIndex; // name → index in m_allEmotes

    std::string m_timelast; // server timestamp for incremental fetches

    std::atomic<bool> m_loaded{false};
    std::atomic<bool> m_fetching{false};

    // disk catalog TTL (30 days)
    static constexpr int64_t CATALOG_TTL_SECONDS = 30 * 24 * 60 * 60;
};

} // namespace paimon::emotes
