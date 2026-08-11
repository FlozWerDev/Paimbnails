#include "TwitchIrcSource.hpp"

#include "../../../utils/WebSocketClient.hpp"

#include <random>

namespace paimon::twitch {

namespace {

constexpr char const* kHost = "irc-ws.chat.twitch.tv";
// Twitch drops us silently when the channel does not exist, so the JOIN gets
// this many seconds to answer with a ROOMSTATE before we call it a bad name.
constexpr float kJoinTimeout = 12.f;

std::string anonymousNick() {
    std::random_device device;
    std::mt19937 engine(device());
    std::uniform_int_distribution<int> digits(10000, 99999);
    return "justinfan" + std::to_string(digits(engine));
}

std::string unescapeTag(std::string_view value) {
    std::string result;
    result.reserve(value.size());
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] != '\\' || i + 1 >= value.size()) {
            result.push_back(value[i]);
            continue;
        }
        switch (value[++i]) {
            case 's': result.push_back(' '); break;
            case ':': result.push_back(';'); break;
            case 'r': result.push_back('\r'); break;
            case 'n': result.push_back('\n'); break;
            case '\\': result.push_back('\\'); break;
            default: result.push_back(value[i]); break;
        }
    }
    return result;
}

std::string tagValue(std::string_view tags, std::string_view key) {
    size_t start = 0;
    while (start < tags.size()) {
        size_t end = tags.find(';', start);
        if (end == std::string_view::npos) end = tags.size();
        auto tag = tags.substr(start, end - start);
        size_t equal = tag.find('=');
        if (tag.substr(0, equal) == key) {
            return equal == std::string_view::npos
                ? std::string{}
                : unescapeTag(tag.substr(equal + 1));
        }
        start = end + 1;
    }
    return {};
}

} // namespace

TwitchIrcSource::TwitchIrcSource(std::string channel, ChatCallbacks callbacks)
    : ChatSourceBase(std::move(callbacks)), m_channel(std::move(channel)) {}

TwitchIrcSource::~TwitchIrcSource() {
    if (m_socket) m_socket->disconnect();
}

void TwitchIrcSource::start() {
    m_nick = anonymousNick();
    m_buffer.clear();
    m_joined = false;
    status("Conectando al chat de #" + m_channel + "...");

    m_socket = std::make_unique<paimon::net::WebSocketClient>();
    paimon::net::WebSocketClient::Options options;
    options.host = kHost;
    options.label = "Twitch";

    bool const started = m_socket->connect(
        std::move(options),
        [this] { onMain([this] { handleOpen(); }); },
        [this](std::string message) {
            onMain([this, message = std::move(message)]() mutable {
                handleMessage(std::move(message));
            });
        },
        [this](std::string error) {
            onMain([this, error = std::move(error)]() mutable {
                fail(error.empty() ? "Chat desconectado" : std::move(error));
            });
        }
    );
    if (!started) {
        fail("El chat de Twitch solo funciona en Windows");
        return;
    }

    later(kJoinTimeout, [this] {
        if (stopped() || m_joined) return;
        fail("No encontramos el canal #" + m_channel + "; revisa el usuario");
    });
}

void TwitchIrcSource::stop() {
    ChatSourceBase::stop();
    if (m_socket) m_socket->disconnect();
}

bool TwitchIrcSource::isOpen() const {
    return m_socket && m_socket->isOpen();
}

void TwitchIrcSource::handleOpen() {
    if (stopped() || !m_socket) return;
    m_socket->send("CAP REQ :twitch.tv/tags twitch.tv/commands\r\n");
    m_socket->send("PASS SCHMOOPIIE\r\n");
    m_socket->send("NICK " + m_nick + "\r\n");
    m_socket->send("JOIN #" + m_channel + "\r\n");
    status("Entrando al chat de #" + m_channel + "...");
}

void TwitchIrcSource::handleMessage(std::string message) {
    if (stopped()) return;
    m_buffer += message;
    if (m_buffer.size() > 64 * 1024) {
        m_buffer.clear();
        return;
    }

    size_t newline = 0;
    while ((newline = m_buffer.find('\n')) != std::string::npos) {
        std::string line = m_buffer.substr(0, newline);
        m_buffer.erase(0, newline + 1);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        handleLine(line);
        if (stopped()) return;
    }
}

void TwitchIrcSource::handleLine(std::string_view line) {
    if (line.starts_with("PING")) {
        if (m_socket) m_socket->send("PONG" + std::string(line.substr(4)) + "\r\n");
        return;
    }

    // Chat first: otherwise a viewer typing RECONNECT or ROOMSTATE in the chat
    // would look like a server command and drop the connection.
    size_t commandPos = line.find(" PRIVMSG #");
    if (commandPos != std::string_view::npos) {
        size_t messagePos = line.find(" :", commandPos);
        if (messagePos == std::string_view::npos) return;

        std::string requester;
        if (!line.empty() && line.front() == '@') {
            size_t tagsEnd = line.find(' ');
            if (tagsEnd != std::string_view::npos) {
                requester = tagValue(line.substr(1, tagsEnd - 1), "display-name");
            }
        }
        if (requester.empty()) {
            // :nick!user@host PRIVMSG ... , with the tags block skipped if present.
            size_t prefix = 0;
            if (line.front() == '@') {
                auto tagged = line.find(" :");
                prefix = tagged == std::string_view::npos ? 0 : tagged + 2;
            } else if (line.front() == ':') {
                prefix = 1;
            }
            size_t bang = line.find('!', prefix);
            if (bang != std::string_view::npos && bang < commandPos) {
                requester = std::string(line.substr(prefix, bang - prefix));
            }
        }
        if (requester.empty()) requester = "Twitch";

        deliver(std::move(requester), std::string(line.substr(messagePos + 2)));
        return;
    }

    if (line.find(" RECONNECT") != std::string_view::npos) {
        fail("Twitch pidio reconectar");
        return;
    }

    if (line.find(" NOTICE ") != std::string_view::npos) {
        if (line.find("authentication failed") != std::string_view::npos) {
            fail("Twitch rechazo la conexion anonima");
            return;
        }
        if (line.find("msg_channel_suspended") != std::string_view::npos) {
            fail("El canal #" + m_channel + " esta suspendido");
            return;
        }
    }

    // ROOMSTATE only arrives for channels that really exist, so it is what
    // confirms the username. The JOIN echo comes back even for a bad name.
    if (line.find(" ROOMSTATE #" + m_channel) != std::string_view::npos) {
        m_joined = true;
        ready("Escuchando el chat de #" + m_channel);
    }
}

} // namespace paimon::twitch
