#include "WebAccountVerification.hpp"

#include "../TwitchRequestManager.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../utils/AccountVerifier.hpp"
#include "../../../utils/Base64.hpp"
#include "../../../utils/HttpClient.hpp"
#include "../../../utils/MainThreadDelay.hpp"
#include "../../../utils/WebHelper.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/GJAccountManager.hpp>

#include <charconv>
#include <chrono>
#include <limits>

using namespace geode::prelude;

namespace paimon::twitch {
namespace {

VerificationIdentity currentIdentity() {
    auto account = AccountVerifier::get().verify();
    return {account.accountID, std::move(account.username)};
}

bool isCurrent(VerificationIdentity const& identity) {
    return !paimon::isRuntimeShuttingDown()
        && RequestVerificationFlow::sameIdentity(identity, currentIdentity());
}

VerificationIdentity responseIdentity(matjson::Value const& response) {
    auto id = response["accountID"].asInt().unwrapOr(0);
    return {id > 0 && id <= std::numeric_limits<int>::max() ? static_cast<int>(id) : 0,
        response["username"].asString().unwrapOr("")};
}

std::string commentKey(int accountID) {
    return fmt::format("web-requests-verification-comment-{}", accountID);
}

int positiveCommentID(std::string response) {
    auto first = response.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return 0;
    auto last = response.find_last_not_of(" \t\r\n");
    int id = 0;
    auto parsed = std::from_chars(response.data() + first, response.data() + last + 1, id);
    return parsed.ec == std::errc{} && parsed.ptr == response.data() + last + 1 && id > 0 ? id : 0;
}

void postOfficial(VerificationIdentity const& identity, std::string endpoint,
    std::string fields, std::function<void(bool, std::string)> callback) {
    auto* account = GJAccountManager::get();
    if (!isCurrent(identity) || !account || account->m_GJP2.empty()) {
        callback(false, {});
        return;
    }
    // credentials never leave this fixed official origin, even through an HTTP redirect.
    std::string body = fmt::format("accountID={}&gjp2={}&secret=Wmfd2893gb7&{}",
        identity.accountID, HttpClient::encodeQueryParamForm(std::string(account->m_GJP2)), fields);
    web::WebRequest request;
    request.header("Content-Type", "application/x-www-form-urlencoded")
        .userAgent("").followRedirects(false).timeout(asp::Duration::fromSecs(15))
        .bodyString(std::move(body));
    WebHelper::dispatch(std::move(request), "POST", "https://www.boomlings.com/database/" + endpoint,
        [callback = std::move(callback)](web::WebResponse response) {
            callback(response.ok(), response.string().unwrapOr(""));
        });
}

} // namespace

std::shared_ptr<RequestVerificationFlow> createWebAccountVerification() {
    static std::weak_ptr<RequestVerificationFlow> active;
    static VerificationIdentity activeIdentity;
    auto identity = currentIdentity();
    if (identity.accountID <= 0 || identity.username.empty()) return {};
    if (auto flow = active.lock(); flow && RequestVerificationFlow::sameIdentity(identity, activeIdentity)) return flow;
    VerificationActions actions;
    actions.isCurrentAccount = [identity] { return isCurrent(identity); };
    actions.requestCode = [identity](auto callback) {
        HttpClient::get().startAccountVerification(identity.username,
            [callback = std::move(callback)](bool ok, std::string const& body) {
                auto json = matjson::parse(body);
                if (!ok || !json) {
                    callback({false, {}, {}, "No se pudo generar el codigo. Reintenta en unos momentos."});
                    return;
                }
                auto const& value = json.unwrap();
                callback({value["success"].asBool().unwrapOr(false), responseIdentity(value),
                    value["code"].asString().unwrapOr(""), {}});
            });
    };
    actions.publishComment = [identity](std::string const& code, auto callback) {
        if (!RequestVerificationFlow::validCode(code)) { callback(false, 0, "Codigo invalido."); return; }
        std::vector<uint8_t> bytes(code.begin(), code.end());
        auto encoded = paimon::base64Encode(bytes);
        std::replace(encoded.begin(), encoded.end(), '+', '-');
        std::replace(encoded.begin(), encoded.end(), '/', '_');
        auto fields = "comment=" + HttpClient::encodeQueryParamForm(encoded)
            + "&cType=1&userName=" + HttpClient::encodeQueryParamForm(identity.username);
        postOfficial(identity, "uploadGJAccComment20.php", std::move(fields),
            [callback = std::move(callback)](bool ok, std::string body) {
                auto id = ok ? positiveCommentID(std::move(body)) : 0;
                callback(id > 0, id, id > 0 ? "" : "GD no confirmo el comentario. Revisa tu perfil; puedes usar el modo manual.");
            });
    };
    actions.checkComment = [identity](auto callback) {
        auto body = matjson::makeObject({{"username", identity.username}});
        HttpClient::get().postWithoutModCode("/api/request-verify/check", body.dump(),
            [callback = std::move(callback)](bool ok, std::string const& response) {
                auto json = matjson::parse(response);
                if (!json) { callback({false, false, {}, {}, "El servidor no respondio correctamente. Reintenta."}); return; }
                auto const& value = json.unwrap();
                bool const pending = value["code"].asString().unwrapOr("") == "CODE_NOT_FOUND";
                callback({ok && value["success"].asBool().unwrapOr(false), pending,
                    responseIdentity(value), value["token"].asString().unwrapOr(""),
                    pending ? "El codigo aun no aparece o ya vencio. Genera otro si hace falta."
                        : "No se pudo comprobar el comentario. Reintenta mas tarde."});
            });
    };
    actions.deleteComment = [identity](int id, auto callback) {
        if (id <= 0) { callback(false, "No hay un comentario temporal identificado."); return; }
        postOfficial(identity, "deleteGJAccComment20.php",
            fmt::format("commentID={}&targetAccountID={}&cType=1", id, identity.accountID),
            [callback = std::move(callback)](bool ok, std::string body) {
                bool const deleted = ok && positiveCommentID(std::move(body)) == 1;
                callback(deleted, deleted ? "" : "Reintenta el borrado o elimina el comentario desde tu perfil.");
            });
    };
    actions.storeToken = [identity](std::string const& token) {
        if (!isCurrent(identity)) return;
        HttpClient::get().setViewerToken(token);
        paimon::requestDeferredModSave();
    };
    actions.rememberComment = [identity](int id, std::string const&) {
        Mod::get()->setSavedValue<int>(commentKey(identity.accountID), id);
        paimon::requestDeferredModSave();
    };
    actions.reconnect = [] { TwitchRequestManager::get().restartWebRequests(); };
    actions.schedule = [](std::function<void()> callback) {
        paimon::scheduleMainThreadDelay(3.f, [callback = std::move(callback)] { callback(); });
    };
    auto pending = Mod::get()->getSavedValue<int>(commentKey(identity.accountID), 0);
    auto flow = std::make_shared<RequestVerificationFlow>(identity, std::move(actions), std::max(0, pending));
    activeIdentity = identity;
    active = flow;
    return flow;
}

std::string webAccountVerificationInfo() {
    return "Comprueba que la cuenta de GD es tuya para abrir o recuperar tu pagina.\n\n"
        "<cg>Automatico:</c> al autorizarlo, publica un codigo temporal en tu perfil de GD. "
        "El servidor lee ese comentario publico, emite un token y el mod lo guarda en este equipo. "
        "Despues borra <cy>solo el comentario que acaba de crear</c>. "
        "Si falla el borrado, puedes reintentarlo o borrarlo desde tu perfil.\n\n"
        "La credencial de GD se usa <cy>solo contra los servidores oficiales de Geometry Dash</c>; "
        "no se envia a Paimbnails. El backend recibe tu nombre, ID publico y el comentario.\n\n"
        "<cj>Manual:</c> copias el codigo, lo publicas y pulsas Comprobar. "
        "El mod no publica ni borra comentarios en este modo; tu eliminas el codigo despues.\n\n"
        "El codigo dura unos 10 minutos. No hace falta cerrar el juego. "
        "Cerrar este panel cancela la verificacion e intenta limpiar un comentario automatico pendiente.";
}

} // namespace paimon::twitch
