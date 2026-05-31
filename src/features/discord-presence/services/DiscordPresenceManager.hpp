#pragma once

#include "../model/PresencePayload.hpp"

#include <atomic>
#include <memory>
#include <unordered_map>

class GJGameLevel;

namespace paimon::discord {

class DiscordPresenceManager {
public:
    static DiscordPresenceManager& get();

    void init();
    void shutdown();
    void refreshSoon();
    void refreshNow();
    void setTemporaryContext(std::string const& key, std::string const& state, std::string const& details = "");
    void clearTemporaryContext(std::string const& key);

private:
    DiscordPresenceManager() = default;
    PresencePayload buildPayload();
    PresencePayload buildScenePayload();
    PresencePayload applyAssetFallbacks(PresencePayload payload);
    bool isIdle() const;
    bool isFocused() const;
    std::string resolveDifficultyAsset(GJGameLevel* level) const;
    std::string sanitizeLevelTitle(std::string const& name) const;
    std::string sanitizeCreatorName(std::string const& name) const;

private:
    bool m_initialized = false;
    bool m_shutdown = false;
    bool m_refreshScheduled = false;
    int64_t m_startTimestamp = 0;
    PresencePayload m_lastPayload;
    std::unordered_map<std::string, PresencePayload> m_temporaryContexts;
    std::shared_ptr<std::atomic<bool>> m_workerToken;
};

} // namespace paimon::discord
