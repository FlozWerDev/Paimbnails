#pragma once

#include <Geode/Geode.hpp>

#include "TwitchRequestFilters.hpp"
#include "RequestRouting.hpp"
#include "sources/ChatSource.hpp"
#include "sources/WebRequestSource.hpp"

#include <array>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace paimon::twitch {

struct ParsedRequest;

struct LevelRequest {
    int64_t entryID = 0;
    int levelID = 0;
    std::string webRequestID;
    int requesterAccountID = 0;
    std::string requester;
    bool requesterVerified = false;
    std::string message;
    std::string description;
    std::string queue = "General";
    std::string command;
    std::string rewardID;
    std::string sourceName;
    std::string eventID;
    std::string userID;
    int64_t receivedAt = 0;
    bool played = false;  // already reviewed on stream.
    int percent = 0;      // saved normal-mode progress.
    std::string videoUrl; // video sent with the request.
    Platform platform = Platform::Twitch;  // request source.
};

std::string requestNote(LevelRequest const& request);

// each platform uses its public web client; only a channel name is required.
enum class ConnectionState {
    Disabled,      // master toggle off.
    NeedsChannel,  // no channel configured.
    Offline,       // paused by the layer.
    Connecting,
    Connected,
    Error,
};

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
    bool isActive(Platform platform) const;  // has a channel (web: enabled).
    size_t activeCount() const;
    size_t connectedCount() const;

    // public web source, enabled through the gd account.
    bool webEnabled() const;
    void setWebEnabled(bool enabled);
    // registered web user and full url; empty until confirmed.
    std::string const& webUser() const { return m_webUser; }
    std::string webUrl() const;

    // ui selection; connections are independent.
    Platform selected() const { return m_selected; }
    void select(Platform platform);

    std::string channelSetting(Platform platform) const;
    void setChannelSetting(Platform platform, std::string value);
    std::string commandsSetting() const;
    void setCommandsSetting(std::string value);
    std::string commandsSetting(Platform platform) const;
    void setCommandsSetting(Platform platform, std::string value);

    RequestRoutingConfig const& routing() const { return m_routing; }
    void setRouting(RequestRoutingConfig config);
    std::string const& lastRewardID() const { return m_lastRewardID; }
    std::string const& lastRewardText() const { return m_lastRewardText; }
    uint64_t rewardDetectionRevision() const { return m_rewardDetectionRevision; }
    std::string const& lastIntakeStatus() const { return m_lastIntakeStatus; }

    std::string const& selectedQueue() const { return m_selectedQueue; }
    void selectQueue(std::string queue);
    std::vector<std::string> queueNames() const;
    bool inSelectedQueue(LevelRequest const& request) const;
    size_t selectedRequestCount() const;

    // chat visibility, separate from the feature toggle.
    bool isLive() const { return m_live; }
    void setLive(bool live);

    bool isAccepting() const { return m_accepting; }
    void setAccepting(bool accepting);

    // pick the next request randomly instead of fifo.
    bool isRandomOrder() const { return m_randomOrder; }
    void setRandomOrder(bool random);

    // accepted mode, difficulty, and length.
    RequestFilters const& filters() const { return m_filters; }
    void setFilters(RequestFilters filters);

    std::vector<LevelRequest> requests() const { return m_requests; }
    size_t requestCount() const { return m_requests.size(); }
    size_t pendingCount() const;
    // filtered requests remain stored.
    size_t filteredCount() const;
    size_t removeFiltered();
    uint64_t queueRevision() const { return m_queueRevision; }

    // index of the next unreviewed request.
    std::optional<size_t> nextPendingIndex() const;
    void markPlayed(size_t index, int percent);
    // explicit reviewed flag toggle used by the queue list.
    void setReviewed(size_t index, bool reviewed);
    void setPercent(size_t index, int percent);
    bool sendWebFeedback(LevelRequest const& request, std::string decision, int percent,
        std::string note, std::string reason, std::string image,
        std::function<void(bool, std::string)> callback);

    void remove(size_t index);
    void moveToFront(size_t index);
    void clear();
    int maxQueueSize() const;

    // live-session counters, reset when the layer calls resetSessionStats.
    struct SessionStats {
        int received = 0;   // requests that entered the queue this session.
        int played = 0;     // marked reviewed/played this session.
        int removed = 0;    // skipped/removed/cleared this session.
    };
    SessionStats const& sessionStats() const { return m_session; }
    void resetSessionStats();
    // mean seconds a still-pending request has been waiting (0 when empty).
    double averageWaitSeconds() const;
    // oldest still-pending request age in seconds (0 when empty).
    int64_t oldestPendingSeconds() const;

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
    void addRequest(Platform platform, ChatMessage message);
    std::string addWebRequest(WebRequest incoming);
    std::string enqueueRequest(
        Platform platform,
        std::string requester,
        std::string message,
        ParsedRequest parsed,
        bool requesterVerified = false,
        std::string webRequestID = std::string{},
        int requesterAccountID = 0,
        std::string queue = "General",
        ChatMessage metadata = ChatMessage{},
        std::string sourceName = std::string{}
    );

    void loadQueue();
    void saveQueue();
    void loadRouting();
    void saveRouting();
    bool rememberEvent(std::string const& key);

    bool m_initialized = false;
    bool m_shuttingDown = false;
    bool m_accepting = true;
    bool m_live = true;
    bool m_randomOrder = false;
    RequestFilters m_filters;
    RequestRoutingConfig m_routing;
    std::string m_selectedQueue;
    std::string m_lastRewardID;
    std::string m_lastRewardText;
    std::string m_lastIntakeStatus;
    uint64_t m_rewardDetectionRevision = 0;
    std::deque<std::string> m_recentEvents;
    std::unordered_set<std::string> m_seenEvents;

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
    int64_t m_nextEntryID = 1;
    std::vector<LevelRequest> m_requests;
    std::unordered_map<std::string, int64_t> m_lastRequestAt;
    SessionStats m_session;
};

}
