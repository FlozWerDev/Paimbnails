#pragma once

#include <chrono>
#include <cstdint>
#include <string>

namespace paimon::discord {

// Discord activity types (matches Discord's numeric enum).
enum class DiscordActivityType : int {
    Playing = 0,
    Listening = 2,
    Watching = 3,
    Competing = 5,
};

struct DiscordActivity {
    std::string state;
    std::string details;
    std::string largeImage;
    std::string largeText;
    std::string smallImage;
    std::string smallText;
    int64_t startTimestamp = 0;
    DiscordActivityType type = DiscordActivityType::Playing;
    std::string button1Label, button1Url;
    std::string button2Label, button2Url;
};

// Best-effort IPC client: lazy connect, no-ops if Discord is absent.
class DiscordIpcClient {
public:
    static DiscordIpcClient& get();

    void setClientID(std::string id) { m_clientID = std::move(id); }

    void update(DiscordActivity const& activity);
    // Clears presence, keeps connection.
    void clear();
    void close();

    // Bumped on (re)connect/teardown; lets manager detect reconnects.
    uint64_t connectionGeneration() const { return m_connectionGeneration; }

private:
    DiscordIpcClient() = default;
    ~DiscordIpcClient();
    DiscordIpcClient(DiscordIpcClient const&) = delete;
    DiscordIpcClient& operator=(DiscordIpcClient const&) = delete;

    bool ensureConnected();
    bool tryConnect();
    bool writeFrame(uint32_t opcode, std::string const& payload);
    // False if peer closed or sent ERROR/CLOSE (caller disconnects).
    bool drainReads();
    void handleDisconnect();

    std::string m_clientID;
    bool m_connected = false;
    uint32_t m_nonce = 0;
    std::chrono::steady_clock::time_point m_lastConnectAttempt{};
    uint64_t m_connectionGeneration = 0;

#ifdef GEODE_IS_WINDOWS
    void* m_pipe = nullptr; // HANDLE
#else
    int m_socket = -1;
#endif
};

} // namespace paimon::discord
