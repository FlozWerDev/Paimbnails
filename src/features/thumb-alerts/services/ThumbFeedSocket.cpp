#include "ThumbFeedSocket.hpp"

#include "NewThumbWatcher.hpp"
#include "../ThumbAlerts.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../core/modules/ModuleRegistry.hpp"
#include "../../../utils/HttpClient.hpp"
#include "../../../utils/MainThreadDelay.hpp"
#include "../../../utils/Debug.hpp"
#include "../../../utils/WebSocketClient.hpp"

#include <Geode/Geode.hpp>

#include <algorithm>
#include <random>

using namespace geode::prelude;

namespace paimon::thumbalerts {

namespace {

constexpr int kShards = 4;
constexpr float kKeepaliveSeconds = 45.f;
constexpr float kFirstRetrySeconds = 5.f;
constexpr float kMaxRetrySeconds = 300.f;

// The server URL is a full origin; the socket needs the bare host.
std::string hostOf(std::string const& url) {
    std::string host = url;
    if (auto scheme = host.find("://"); scheme != std::string::npos) {
        host = host.substr(scheme + 3);
    }
    if (auto slash = host.find('/'); slash != std::string::npos) {
        host = host.substr(0, slash);
    }
    return host;
}

int pickShard() {
    static std::mt19937 rng{std::random_device{}()};
    return std::uniform_int_distribution<int>(0, kShards - 1)(rng);
}

// The setting only exists on Windows, which is also the only platform where
// WebSocketClient has an implementation.
bool liveWanted() {
#ifdef GEODE_IS_WINDOWS
    return Mod::get()->getSettingValue<bool>("thumbalert-live");
#else
    return false;
#endif
}

} // namespace

ThumbFeedSocket& ThumbFeedSocket::get() {
    static ThumbFeedSocket instance;
    return instance;
}

void ThumbFeedSocket::start() {
    if (m_started) return;
    m_started = true;

#ifdef GEODE_IS_WINDOWS
    listenForSettingChanges<bool>("thumbalert-live", [](bool live) {
        if (live) ThumbFeedSocket::get().connect();
        else ThumbFeedSocket::get().stop();
    });
#endif

    this->connect();
}

void ThumbFeedSocket::stop() {
    ++m_generation;
    m_connecting = false;
    m_connected = false;
    if (m_socket) {
        m_socket->disconnect();
        m_socket.reset();
    }
}

void ThumbFeedSocket::connect() {
    if (paimon::isRuntimeShuttingDown()) return;
    if (m_connecting || m_connected) return;
    if (!liveWanted()) return;
    if (!Mod::get()->getSettingValue<bool>("thumbalert-enabled")) return;
    if (!paimon::modules::isEnabled(kModuleId)) return;

    auto const& client = HttpClient::get();
    auto host = hostOf(client.getServerURL());
    if (host.empty()) return;

    auto socket = std::make_unique<paimon::net::WebSocketClient>();
    paimon::net::WebSocketClient::Options options;
    options.host = host;
    options.path = fmt::format("/api/feed/connect?shard={}", pickShard());
    options.label = "Paimbnails feed";
    options.headers = {{"X-API-Key", client.getApiKey()}};

    m_connecting = true;
    auto const generation = ++m_generation;

    // Every callback lands on the socket's reader thread; nothing here may
    // touch cocos or the save file before hopping to the main thread.
    bool const started = socket->connect(
        std::move(options),
        [generation] {
            Loader::get()->queueInMainThread([generation] {
                auto& self = ThumbFeedSocket::get();
                if (generation != self.m_generation) return;
                self.m_connecting = false;
                self.m_connected = true;
                self.m_attempt = 0;
                PaimonDebug::log("[ThumbFeed] live feed connected");
                self.scheduleKeepalive();
            });
        },
        [generation](std::string message) {
            Loader::get()->queueInMainThread([generation, message = std::move(message)] {
                if (generation != ThumbFeedSocket::get().m_generation) return;
                if (message == "pong") return;
                NewThumbWatcher::get().onPushMessage(message);
            });
        },
        [generation](std::string reason) {
            Loader::get()->queueInMainThread([generation, reason = std::move(reason)] {
                auto& self = ThumbFeedSocket::get();
                if (generation != self.m_generation) return;
                self.m_connecting = false;
                self.m_connected = false;
                PaimonDebug::warn("[ThumbFeed] live feed closed: {}", reason);
                self.scheduleReconnect();
            });
        }
    );

    if (!started) {
        // No WebSocket implementation on this platform, or the handshake never
        // got off the ground. The poll already covers this case.
        m_connecting = false;
        PaimonDebug::log("[ThumbFeed] live feed unavailable, staying on the poll");
        return;
    }

    m_socket = std::move(socket);
}

void ThumbFeedSocket::scheduleKeepalive() {
    auto const generation = m_generation;
    paimon::scheduleMainThreadDelay(kKeepaliveSeconds, [generation] {
        auto& self = ThumbFeedSocket::get();
        if (generation != self.m_generation || !self.m_connected) return;
        if (paimon::isRuntimeShuttingDown()) return;
        // Answered by the runtime without waking the Durable Object.
        if (self.m_socket && self.m_socket->send("ping")) self.scheduleKeepalive();
    });
}

void ThumbFeedSocket::scheduleReconnect() {
    if (paimon::isRuntimeShuttingDown()) return;
    if (!liveWanted()) return;

    m_attempt = std::min(m_attempt + 1, 6);
    float const delay = std::min(kFirstRetrySeconds * static_cast<float>(1 << (m_attempt - 1)),
                                 kMaxRetrySeconds);
    auto const generation = m_generation;
    paimon::scheduleMainThreadDelay(delay, [generation] {
        auto& self = ThumbFeedSocket::get();
        if (generation != self.m_generation) return;
        if (self.m_socket) self.m_socket.reset();
        self.connect();
    });
}

} // namespace paimon::thumbalerts
