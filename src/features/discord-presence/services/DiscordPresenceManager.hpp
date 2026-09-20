#pragma once

#include "../model/PresencePayload.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

class GJGameLevel;

namespace paimon::discord {

class DiscordPresenceManager {
public:
    static DiscordPresenceManager& get();
    static bool isSupported();

    void init();
    void shutdown();
    void refreshSoon();
    void refreshNow(bool force = false);
    void setTemporaryContext(std::string const& key, std::string const& state, std::string const& details = "");
    void clearTemporaryContext(std::string const& key);

private:
    DiscordPresenceManager() = default;
    void ensureWorker();
    PresencePayload buildPayload();
    PresencePayload buildScenePayload();
    PresencePayload applyAssetFallbacks(PresencePayload payload);
    bool isIdle() const;
    bool isFocused() const;
    std::string resolveDifficultyAsset(GJGameLevel* level) const;
    std::string sanitizeLevelTitle(std::string const& name) const;
    std::string sanitizeCreatorName(std::string const& name) const;

private:
    struct TemporaryEntry {
        PresencePayload payload;
        uint64_t seq = 0;
    };

    bool m_initialized = false;
    bool m_shutdown = false;
    bool m_refreshScheduled = false;
    bool m_presenceCleared = false;
    int64_t m_startTimestamp = 0;
    PresencePayload m_lastPayload;
    std::string m_lastActivityType;
    bool m_lastShowTimestamp = false;
    uint64_t m_seenGeneration = 0;
    uint64_t m_tempSeq = 0;
    std::unordered_map<std::string, TemporaryEntry> m_temporaryContexts;
    std::shared_ptr<std::atomic<bool>> m_workerToken;
};

} // namespace paimon::discord
