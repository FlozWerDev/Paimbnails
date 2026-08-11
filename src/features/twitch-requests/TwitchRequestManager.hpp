#pragma once

#include <Geode/Geode.hpp>

#include "TwitchRequestFilters.hpp"
#include "sources/ChatSource.hpp"
#include "sources/WebRequestSource.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace paimon::twitch {

struct ParsedRequest;

struct LevelRequest {
    int levelID = 0;
    std::string requester;
    bool requesterVerified = false;
    std::string message;
    int64_t receivedAt = 0;
    bool played = false;  // Already reviewed on stream.
    int percent = 0;      // Saved normal-mode progress.
    std::string videoUrl; // Video sent with the request.
    Platform platform = Platform::Twitch;  // Request source.
};

// Free-form note from the web source; chat command lines are not notes.
std::string requestNote(LevelRequest const& request);

// Each platform uses its public web client; only a channel name is required.
enum class ConnectionState {
    Disabled,      // Master toggle off.
    NeedsChannel,  // No channel configured.
    Offline,       // Paused by the layer.
    Connecting,
    Connected,
    Error,
};

// Maintains one connection/status per configured platform and one request queue.
class TwitchRequestManager final {
public:
    static TwitchRequestManager& get();

    void init();
    void shutdown();
    void restart();
    void restart(Platform platform);
    void restartWebRequests();

    ConnectionState state(Platform platform) const;
    std::string const& statusText(Platform platform) const;
    std::string const& channel(Platform platform) const;
    bool isActive(Platform platform) const;  // Has a channel (web: enabled).
    size_t activeCount() const;
    size_t connectedCount() const;

    // Public web source, enabled through the GD account.
    bool webEnabled() const;
    void setWebEnabled(bool enabled);
    // Registered web user and full URL; empty until confirmed.
    std::string const& webUser() const { return m_webUser; }
    std::string webUrl() const;

    // UI selection; connections are independent.
    Platform selected() const { return m_selected; }
    void select(Platform platform);

    std::string channelSetting(Platform platform) const;
    void setChannelSetting(Platform platform, std::string value);
    std::string commandsSetting() const;
    void setCommandsSetting(std::string value);

    // Chat visibility, separate from the feature toggle.
    bool isLive() const { return m_live; }
    void setLive(bool live);

    bool isAccepting() const { return m_accepting; }
    void setAccepting(bool accepting);

    // Pick the next request randomly instead of FIFO.
    bool isRandomOrder() const { return m_randomOrder; }
    void setRandomOrder(bool random);

    // Accepted mode, difficulty, and length.
    RequestFilters const& filters() const { return m_filters; }
    void setFilters(RequestFilters filters);

    std::vector<LevelRequest> requests() const { return m_requests; }
    size_t requestCount() const { return m_requests.size(); }
    size_t pendingCount() const;
    // Filtered requests remain stored.
    size_t filteredCount() const;
    size_t removeFiltered();
    uint64_t queueRevision() const { return m_queueRevision; }

    // Index of the next unreviewed request.
    std::optional<size_t> nextPendingIndex() const;
    void markPlayed(size_t index, int percent);
    void setPercent(size_t index, int percent);

    void remove(size_t index);
    void moveToFront(size_t index);
    void clear();
    int maxQueueSize() const;

private:
    struct Link {
        ConnectionState state = ConnectionState::NeedsChannel;
        std::string statusText;
        std::string channel;
        std::unique_ptr<ChatSource> source;
        uint64_t generation = 0;
        bool reconnectScheduled = false;
        int reconnectDelay = 3;
    };

    TwitchRequestManager() = default;

    Link& link(Platform platform);
    Link const& link(Platform platform) const;
    void setState(Platform platform, ConnectionState state, std::string text);

    void connectLink(Platform platform);
    void stopLink(Platform platform);
    void handleError(Platform platform, uint64_t generation, std::string error);
    void scheduleReconnect(Platform platform);
    void connectWebRequests();
    void stopWebRequests();
    void scheduleWebReconnect();
    void setWebState(ConnectionState state, std::string text);
    void scheduleMonitor();
    void monitor();
    void addRequest(Platform platform, std::string requester, std::string message);
    std::string addWebRequest(WebRequest incoming);
    std::string enqueueRequest(
        Platform platform,
        std::string requester,
        std::string message,
        ParsedRequest parsed,
        bool requesterVerified = false
    );

    void loadQueue();
    void saveQueue();

    bool m_initialized = false;
    bool m_shuttingDown = false;
    bool m_accepting = true;
    bool m_live = true;
    bool m_randomOrder = false;
    RequestFilters m_filters;

    Platform m_selected = Platform::Twitch;
    std::array<Link, kPlatformCount> m_links;
    std::unique_ptr<WebRequestSource> m_webSource;
    ConnectionState m_webState = ConnectionState::Disabled;
    std::string m_webStatus;
    std::string m_webUser;
    uint64_t m_webGeneration = 0;
    bool m_webReconnectScheduled = false;
    int m_webReconnectDelay = 3;

    uint64_t m_monitorGeneration = 0;
    uint64_t m_queueRevision = 0;
    std::vector<LevelRequest> m_requests;
    std::unordered_map<std::string, int64_t> m_lastRequestAt;
};

}
