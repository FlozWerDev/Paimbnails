#pragma once

#include "../model/PresencePayload.hpp"
#include "../model/PresenceContext.hpp"
#include "DiscordIpcClient.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <string>

class GJGameLevel;

namespace paimon::discord {

class DiscordActivityMailbox;

class DiscordPresenceManager {
public:
    static DiscordPresenceManager& get();
    static bool isInitialized();
    static bool isSupported();

    void init();
    void resumeAfterConsent();
    void shutdown();
    void refreshSoon();
    void refreshNow(bool force = false);
    DiscordActivity previewActivity();
    std::string connectionStatus() const;
    std::vector<std::string> validationMessages() const;

private:
    DiscordPresenceManager() = default;
    void ensureWorker();
    DiscordActivity buildActivity();
    PresenceContext captureContext(PresenceConfiguration const& configuration);
    bool isFocused() const;
    std::string resolveDifficultyAsset(GJGameLevel* level) const;

private:
    bool m_initialized = false;
    bool m_shutdown = false;
    bool m_refreshScheduled = false;
    bool m_presenceCleared = false;
    int64_t m_startTimestamp = 0;
    DiscordActivity m_lastActivity;
    std::string m_activityKey, m_levelKey, m_attemptKey;
    int64_t m_activityTimestamp = 0, m_levelTimestamp = 0, m_attemptTimestamp = 0;
    std::array<bool, kBooleanOptions.size()> m_lastPrivacyFlags{};
    std::string m_lastSharingScope;
    std::shared_ptr<DiscordActivityMailbox> m_ipcMailbox;
};

} // namespace paimon::discord
