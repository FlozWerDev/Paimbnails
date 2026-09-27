#pragma once

#include "../model/PresencePayload.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

class GJGameLevel;

namespace paimon::discord {

class DiscordPresenceManager {
public:
    static DiscordPresenceManager& get();
    static bool isSupported();

    void init();
    void resumeAfterConsent();
    void shutdown();
    void refreshSoon();
    void refreshNow(bool force = false);

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
    bool m_initialized = false;
    bool m_shutdown = false;
    bool m_refreshScheduled = false;
    bool m_presenceCleared = false;
    int64_t m_startTimestamp = 0;
    PresencePayload m_lastPayload;
    std::string m_lastActivityType;
    bool m_lastShowTimestamp = false;
    uint64_t m_seenGeneration = 0;
    std::shared_ptr<std::atomic<bool>> m_workerToken;
};

} // namespace paimon::discord
