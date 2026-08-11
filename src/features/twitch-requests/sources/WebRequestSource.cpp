#include "WebRequestSource.hpp"

#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../utils/AccountVerifier.hpp"
#include "../../../utils/HttpClient.hpp"
#include "../../../utils/MainThreadDelay.hpp"
#include "../../../utils/WebHelper.hpp"

#include <Geode/Geode.hpp>

#include <cctype>
#include <chrono>
#include <climits>
#include <optional>

using namespace geode::prelude;

namespace paimon::twitch {

namespace {

// Una sesion por cuenta: cambiar de cuenta de GD no puede reutilizar el token de
// la anterior, y la clave vieja (una sola para todas) se lee como respaldo.
constexpr char const* kLegacyTokenKey = "web-requests-host-token";

std::string tokenKey(int accountID) {
    return fmt::format("web-requests-host-token-{}", accountID);
}

// Como se ve el usuario en la URL: el servidor manda el suyo, esto es solo para
// no quedarnos sin nada que mostrar si la respuesta viene corta.
std::string slugify(std::string const& value) {
    std::string slug;
    for (unsigned char ch : value) {
        if (std::isalnum(ch) || ch == '_') slug += static_cast<char>(std::tolower(ch));
    }
    return slug;
}

struct ServerTarget {
    std::string host;
    std::string path;
};

std::optional<ServerTarget> parseServer(std::string value) {
    constexpr std::string_view scheme = "https://";
    if (!value.starts_with(scheme)) return std::nullopt;
    value.erase(0, scheme.size());
    while (!value.empty() && value.back() == '/') value.pop_back();
    auto slash = value.find('/');
    ServerTarget target;
    target.host = value.substr(0, slash);
    if (slash != std::string::npos) target.path = value.substr(slash);
    if (target.host.empty()) return std::nullopt;
    return target;
}

} // namespace

WebRequestSource::WebRequestSource(WebRequestCallbacks callbacks)
  : m_callbacks(std::move(callbacks)) {}

WebRequestSource::~WebRequestSource() {
    stop();
}

bool WebRequestSource::supported() {
#ifdef GEODE_IS_WINDOWS
    return true;
#else
    return false;
#endif
}

void WebRequestSource::start() {
    if (m_stopped) return;
    auto account = AccountVerifier::get().verify();
    if (!account.loggedIn()) {
        fail("Inicia sesion en Geometry Dash para tener tu pagina");
        return;
    }
    m_accountID = account.accountID;
    m_username = std::move(account.username);
    m_slug = slugify(m_username);
    if (m_slug.empty()) {
        fail("Tu nombre de GD no sirve para una direccion web");
        return;
    }
    m_serverBase = HttpClient::get().getServerURL();
    if (m_callbacks.onStatus) m_callbacks.onStatus("Registrando tu pagina de requests...");
    registerHost();
}

void WebRequestSource::stop() {
    if (m_stopped) return;
    m_stopped = true;
    m_open = false;
    m_life.reset();
    if (m_socket) {
        m_socket->disconnect();
        m_socket.reset();
    }
}

bool WebRequestSource::isOpen() const {
    return m_open && m_socket && m_socket->isOpen();
}

std::string WebRequestSource::savedToken() const {
    auto token = Mod::get()->getSavedValue<std::string>(tokenKey(m_accountID), "");
    if (token.empty()) token = Mod::get()->getSavedValue<std::string>(kLegacyTokenKey, "");
    return token;
}

void WebRequestSource::registerHost() {
    // El servidor comprueba la cuenta contra los servidores de RobTop, asi que
    // manda el nombre tal cual: el de la URL lo decide el.
    auto token = savedToken();
    auto body = matjson::makeObject({
        {"username", m_username},
        {"accountID", m_accountID},
    });
    web::WebRequest request;
    request.header("Content-Type", "application/json")
        .header("X-API-Key", HttpClient::get().getApiKey())
        .timeout(std::chrono::seconds(12))
        .bodyString(body.dump(matjson::NO_INDENTATION));
    if (!token.empty()) request.header("Authorization", "Bearer " + token);

    std::weak_ptr<uint8_t> life = m_life;
    WebHelper::dispatch(std::move(request), "POST", m_serverBase + "/api/request-host/register",
        [this, life](web::WebResponse response) {
            if (life.expired() || m_stopped || paimon::isRuntimeShuttingDown()) return;
            auto parsed = matjson::parse(response.string().unwrapOr(""));
            if (!response.ok() || !parsed) {
                handleRegisterError(response.code(),
                    parsed ? parsed.unwrap()["code"].asString().unwrapOr("") : std::string{});
                return;
            }
            auto newToken = parsed.unwrap()["token"].asString().unwrapOr("");
            if (newToken.size() < 32) {
                fail("El servidor no entrego una sesion segura para requests");
                return;
            }
            // El servidor manda el usuario ya limpio para la URL.
            auto slug = parsed.unwrap()["slug"].asString().unwrapOr("");
            if (!slug.empty()) m_slug = std::move(slug);
            Mod::get()->setSavedValue<std::string>(tokenKey(m_accountID), newToken);
            paimon::requestDeferredModSave();
            connectSocket(std::move(newToken));
        });
}

// Un token guardado que el servidor ya no reconoce se tira y se vuelve a pedir
// una vez: si no, reinstalar el mod dejaria la pagina inservible para siempre.
void WebRequestSource::handleRegisterError(int status, std::string code) {
    if (code == "TOKEN_REQUIRED" && !m_retriedWithoutToken) {
        m_retriedWithoutToken = true;
        Mod::get()->setSavedValue<std::string>(tokenKey(m_accountID), std::string{});
        Mod::get()->setSavedValue<std::string>(kLegacyTokenKey, std::string{});
        paimon::requestDeferredModSave();
        registerHost();
        return;
    }

    std::string error = "No pudimos registrar tu pagina de requests";
    if (code == "TOKEN_REQUIRED") {
        error = "Esta pagina ya esta registrada; restablece su acceso en el servidor";
    } else if (code == "USERNAME_TAKEN") {
        error = "Otra cuenta de GD ya registro esa direccion";
    } else if (code == "ACCOUNT_MISMATCH" || code == "USERNAME_MISMATCH"
        || code == "ACCOUNT_NOT_FOUND") {
        error = "El servidor no pudo verificar tu cuenta de Geometry Dash";
    } else if (code == "VERIFY_FAILED") {
        error = "No se pudo comprobar tu cuenta; reintentando...";
    } else if (code == "RATE_LIMITED") {
        error = "Demasiados intentos seguidos; espera un momento";
    } else if (code == "AUTH_FAILED" || status == 401) {
        error = "El servidor rechazo la clave del mod";
    } else if (status == 404) {
        error = "El servidor todavia no tiene la pagina de requests";
    }
    fail(std::move(error));
}

void WebRequestSource::connectSocket(std::string token) {
    auto target = parseServer(m_serverBase);
    if (!target) {
        fail("La direccion del servidor de requests no es valida");
        return;
    }
    m_socket = std::make_unique<paimon::net::WebSocketClient>();
    paimon::net::WebSocketClient::Options options;
    options.host = std::move(target->host);
    options.path = target->path + "/api/request-host/connect?username="
        + HttpClient::encodeQueryParam(m_slug);
    options.label = "Paimbnails Web Requests";
    options.headers = {
        {"X-API-Key", HttpClient::get().getApiKey()},
        {"Authorization", "Bearer " + token},
    };

    std::weak_ptr<uint8_t> life = m_life;
    bool started = m_socket->connect(
        std::move(options),
        [this, life] {
            if (life.expired()) return;
            m_open = true;
            onMain([this] {
                if (m_callbacks.onReady) m_callbacks.onReady(m_slug);
            });
        },
        [this, life](std::string message) {
            if (life.expired()) return;
            onMain([this, message = std::move(message)]() mutable {
                handleMessage(std::move(message));
            });
        },
        [this, life](std::string error) {
            if (life.expired()) return;
            m_open = false;
            onMain([this, error = std::move(error)]() mutable {
                fail(error.empty() ? "La pagina de requests se desconecto" : std::move(error));
            });
        }
    );
    if (!started) fail("Web requests no esta disponible en esta plataforma");
}

void WebRequestSource::handleMessage(std::string message) {
    if (m_stopped || !m_socket) return;
    auto parsed = matjson::parse(message);
    if (!parsed) return;
    auto body = parsed.unwrap();
    if (body["type"].asString().unwrapOr("") != "level-request") return;
    auto requestID = body["requestId"].asString().unwrapOr("");
    auto levelID = body["levelId"].asInt().unwrapOr(0);
    if (requestID.empty() || levelID <= 0 || levelID > INT_MAX) return;

    WebRequest incoming;
    incoming.requester = body["requester"].asString().unwrapOr("Web");
    incoming.requesterVerified = body["requesterVerified"].asBool().unwrapOr(false);
    incoming.levelID = static_cast<int>(levelID);
    incoming.message = body["message"].asString().unwrapOr("");
    incoming.video = body["video"].asString().unwrapOr("");

    std::string reason = m_callbacks.onRequest
        ? m_callbacks.onRequest(std::move(incoming))
        : "disabled";
    auto reply = matjson::makeObject({
        {"type", "request-result"},
        {"requestId", requestID},
        {"accepted", reason.empty()},
        {"reason", reason},
    });
    m_socket->send(reply.dump(matjson::NO_INDENTATION));
}

void WebRequestSource::fail(std::string error) {
    if (m_stopped) return;
    m_open = false;
    if (m_callbacks.onError) m_callbacks.onError(std::move(error));
}

void WebRequestSource::onMain(std::function<void()> work) {
    std::weak_ptr<uint8_t> life = m_life;
    Loader::get()->queueInMainThread([life, work = std::move(work)]() mutable {
        if (life.expired() || paimon::isRuntimeShuttingDown()) return;
        work();
    });
}

} // namespace paimon::twitch
