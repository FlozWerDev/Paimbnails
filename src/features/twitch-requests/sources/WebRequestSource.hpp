#pragma once

#include "../../../utils/WebSocketClient.hpp"

#include <atomic>
#include <functional>
#include <memory>
#include <string>

namespace paimon::twitch {

// The server decides whether the requester's GD identity was verified.
struct WebRequest {
    std::string requester;
    bool requesterVerified = false;
    int levelID = 0;
    std::string message;
    std::string video;
};

struct WebRequestCallbacks {
    std::function<void(std::string)> onStatus;
    std::function<void(std::string)> onReady;   // usuario con el que quedo la URL
    std::function<std::string(WebRequest)> onRequest;
    std::function<void(std::string)> onError;
};

class WebRequestSource final {
public:
    explicit WebRequestSource(WebRequestCallbacks callbacks);
    ~WebRequestSource();

    static bool supported();

    void start();
    void stop();
    bool isOpen() const;

private:
    std::string savedToken() const;
    void registerHost();
    void handleRegisterError(int status, std::string code);
    void connectSocket(std::string token);
    void handleMessage(std::string message);
    void fail(std::string error);
    void onMain(std::function<void()> work);

    WebRequestCallbacks m_callbacks;
    std::unique_ptr<paimon::net::WebSocketClient> m_socket;
    std::shared_ptr<uint8_t> m_life = std::make_shared<uint8_t>(0);
    std::atomic_bool m_open = false;
    bool m_stopped = false;
    bool m_retriedWithoutToken = false;
    int m_accountID = 0;
    std::string m_username;  // como se llama tu cuenta de GD
    std::string m_slug;      // como se escribe en la URL
    std::string m_serverBase;
};

} // namespace paimon::twitch
