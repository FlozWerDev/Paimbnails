#pragma once

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

// Minimal, self-contained Discord Rich Presence client. Talks to the local
// Discord IPC endpoint directly (named pipe on Windows, unix socket on macOS),
// so we don't depend on any external discord-rpc library.
//
// All methods are cheap and safe to call from the main thread. Connection is
// lazy and best-effort: if Discord isn't running the calls just no-op and a
// reconnect is retried later.
class DiscordIpcClient {
public:
    static DiscordIpcClient& get();

    void setClientID(std::string id) { m_clientID = std::move(id); }

    // Sends the activity, connecting/handshaking first if needed.
    void update(DiscordActivity const& activity);
    // Clears the presence (keeps the connection open).
    void clear();
    // Closes the IPC connection.
    void close();

    bool isConnected() const { return m_connected; }

private:
    DiscordIpcClient() = default;
    ~DiscordIpcClient();
    DiscordIpcClient(DiscordIpcClient const&) = delete;
    DiscordIpcClient& operator=(DiscordIpcClient const&) = delete;

    bool ensureConnected();
    bool tryConnect();
    bool writeFrame(uint32_t opcode, std::string const& payload);
    void drainReads();
    void handleDisconnect();

    std::string m_clientID;
    bool m_connected = false;
    uint32_t m_nonce = 0;
    int64_t m_lastConnectAttempt = 0;

#ifdef _WIN32
    void* m_pipe = nullptr; // HANDLE
#else
    int m_socket = -1;
#endif
};

} // namespace paimon::discord
