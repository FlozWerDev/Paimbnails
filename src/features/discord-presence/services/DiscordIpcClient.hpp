#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <mutex>

namespace paimon::discord {

// discord activity types (matches discord's numeric enum).
enum class DiscordActivityType : int {
    Playing = 0,
    Listening = 2,
    Watching = 3,
    Competing = 5,
};

struct DiscordActivity {
    std::string applicationId;
    bool hidden = false; // local sharing policy; never serialized into Discord's activity
    std::string state;
    std::string details;
    std::string largeImage;
    std::string largeText;
    std::string smallImage;
    std::string smallText;
    int64_t startTimestamp = 0;
    int partySize = 0, partyMax = 0;
    DiscordActivityType type = DiscordActivityType::Playing;
    std::string button1Label, button1Url;
    std::string button2Label, button2Url;
    bool operator==(DiscordActivity const&) const = default;
};

// best-effort ipc client: lazy connect, no-ops if discord is absent.
class DiscordIpcClient {
public:
    static DiscordIpcClient& get();

    void setClientID(std::string id);

    void update(DiscordActivity const& activity);
    // clears presence, keeps connection.
    void clear();
    void close();
    void poll();

    // bumped on (re)connect/teardown; lets manager detect reconnects.
    uint64_t connectionGeneration() const { return m_connectionGeneration.load(std::memory_order_acquire); }
    bool isConnected() const { return m_connected.load(std::memory_order_acquire); }
    bool isReady() const { return m_ready.load(std::memory_order_acquire); }
    std::string lastError() const;

private:
    DiscordIpcClient() = default;
    ~DiscordIpcClient();
    DiscordIpcClient(DiscordIpcClient const&) = delete;
    DiscordIpcClient& operator=(DiscordIpcClient const&) = delete;

    bool ensureConnected();
    bool tryConnect();
    bool writeFrame(uint32_t opcode, std::string const& payload);
    // false if peer closed or sent error/close (caller disconnects).
    bool drainReads();
    bool processFrames();
    void setError(std::string message);
    void handleDisconnect();

    std::string m_clientID;
    std::atomic<bool> m_connected{false};
    std::atomic<bool> m_ready{false};
    mutable std::mutex m_errorMutex;
    std::string m_lastError;
    std::string m_receiveBuffer;
    uint32_t m_nonce = 0;
    std::chrono::steady_clock::time_point m_lastConnectAttempt{};
    std::chrono::steady_clock::time_point m_handshakeStarted{};
    std::atomic<uint64_t> m_connectionGeneration{0};

#ifdef GEODE_IS_WINDOWS
    void* m_pipe = nullptr; // handle
#else
    int m_socket = -1;
#endif
};

} // namespace paimon::discord
