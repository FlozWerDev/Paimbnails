#include "CommunityClient.hpp"

#include "../../../core/ModAuthFlow.hpp"
#include "../../../utils/AccountVerifier.hpp"
#include "../../../utils/HttpClient.hpp"
#include "../../../utils/Localization.hpp"
#include "../../../utils/PaimonNotification.hpp"

#include <Geode/ui/GeodeUI.hpp>
#include <charconv>
#include <chrono>

using namespace geode::prelude;

namespace paimon::community {

char const* tx(char const* spanish, char const* english) {
    return Localization::get().getLanguage() == Localization::Language::SPANISH ? spanish : english;
}

std::optional<Identity> currentIdentity() {
    auto account = AccountVerifier::get().verify();
    if (!account.loggedIn()) return std::nullopt;
    return Identity{account.accountID, account.username};
}

bool hasAccountProof() {
    auto& http = HttpClient::get();
    return http.hasSessionToken() || http.hasViewerToken() || !http.getModCode().empty();
}

bool isCommunityId(std::string_view id) {
    return id.size() == 32 && id.find_first_not_of("0123456789abcdef") == std::string_view::npos;
}

namespace {

struct ParsedFailure {
    int status = 0;
    std::string code;
    std::string message;
    std::string details;
};

// httpclient reports failures as "HTTP <code>: <body>".
ParsedFailure parseFailure(std::string const& response) {
    ParsedFailure out;
    if (response.rfind("HTTP ", 0) == 0) {
        auto colon = response.find(':');
        if (colon != std::string::npos) {
            int status = 0;
            auto begin = response.data() + 5;
            std::from_chars(begin, response.data() + colon, status);
            out.status = status;
        }
    }
    auto brace = response.find('{');
    if (brace != std::string::npos) {
        if (auto parsed = matjson::parse(std::string_view(response).substr(brace)); parsed.isOk()) {
            auto const& json = parsed.unwrap();
            out.code = json["code"].asString().unwrapOr("");
            out.message = json["error"].asString().unwrapOr("");
            out.details = json["details"].asString().unwrapOr("");
        }
    }
    return out;
}

std::string identityQuery(Identity const& identity) {
    return "username=" + HttpClient::encodeQueryParam(identity.username)
        + "&accountID=" + std::to_string(identity.accountID);
}

void withIdentity(matjson::Value& body, Identity const& identity) {
    body["username"] = identity.username;
    body["accountID"] = identity.accountID;
}

void handleJson(bool success, std::string const& response, JsonCallback const& cb) {
    if (!success && needsVerification(response)) {
        // one popup per burst: a global icon link can fail several uploads in a row.
        static auto last = std::chrono::steady_clock::time_point{};
        auto now = std::chrono::steady_clock::now();
        if (now - last > std::chrono::seconds(5)) {
            last = now;
            Loader::get()->queueInMainThread([] { promptVerification(); });
        }
    }
    if (!cb) return;
    if (!success) {
        cb(false, matjson::Value(), describeError(response));
        return;
    }
    auto parsed = matjson::parse(response);
    if (!parsed.isOk() || !parsed.unwrap().isObject()) {
        cb(false, matjson::Value(), tx("Respuesta invalida del servidor.", "Invalid server response."));
        return;
    }
    cb(true, parsed.unwrap(), "");
}

std::string notLoggedIn() {
    return tx("Inicia sesion en Geometry Dash para usar Paimon Base.", "Log in to Geometry Dash to use Paimon Base.");
}

} // namespace

bool needsVerification(std::string const& response) {
    auto code = parseFailure(response).code;
    return code == "IDENTITY_REQUIRED" || code == "IDENTITY_MISMATCH";
}

std::string describeError(std::string const& response) {
    auto failure = parseFailure(response);
    auto const& code = failure.code;
    if (code == "IDENTITY_REQUIRED" || code == "IDENTITY_MISMATCH") {
        return tx("Verifica tu cuenta de GD para usar Paimon Base.", "Verify your GD account to use Paimon Base.");
    }
    if (code == "ACCOUNT_REQUIRED") return notLoggedIn();
    if (code == "BANNED") return tx("Tu cuenta esta baneada en Paimbnails.", "Your account is banned from Paimbnails.");
    if (code == "TOO_MANY_PENDING") {
        return tx("Espera a que un moderador revise tus envios pendientes.", "Wait until a moderator reviews your pending submissions.");
    }
    if (code == "PRESET_LIMIT" || code == "BACKGROUND_LIMIT" || code == "ASSET_LIMIT" || code == "ITEM_LIMIT") {
        return tx("Llegaste al limite. Borra algo antiguo antes de subir mas.", "You reached the limit. Delete something old first.");
    }
    if (code == "OWN_ITEM") return tx("No puedes calificar lo que tu enviaste.", "You cannot rate your own submission.");
    if (code == "RATE_LIMITED" || failure.status == 429) {
        return tx("Demasiadas peticiones. Espera un minuto.", "Too many requests. Wait a minute.");
    }
    if (code == "FILE_TOO_LARGE" || code == "PAYLOAD_TOO_LARGE" || code == "CONFIGURATION_TOO_LARGE"
        || code == "BODY_TOO_LARGE" || failure.status == 413) {
        return tx("El archivo o la configuracion pesa demasiado.", "The file or configuration is too large.");
    }
    if (code == "UNSUPPORTED_MEDIA") return tx("Formato no soportado. Usa PNG, JPG o WEBP.", "Unsupported format. Use PNG, JPG or WEBP.");
    if (code == "SECURITY_VIOLATION") return tx("La imagen no paso el escaneo de seguridad.", "The image failed the security scan.");
    if (code == "NOT_FOUND" || code == "NOT_SHARED" || failure.status == 404) return tx("No se encontro.", "Not found.");
    if (code == "NOT_OWNER") return tx("Solo el autor o un moderador puede hacer esto.", "Only the author or a moderator can do this.");
    if (code == "NOT_PENDING") return tx("Otro moderador ya lo reviso.", "Another moderator already reviewed it.");
    if (code.rfind("MOD_", 0) == 0) {
        return tx("Verifica tu cuenta de moderador en el panel de moderacion.", "Verify your moderator account in the moderation panel.");
    }
    if (code == "COMMUNITY_UNAVAILABLE" || failure.status == 503) {
        return tx("Paimon Base no esta disponible ahora. Reintenta luego.", "Paimon Base is unavailable right now. Try again later.");
    }
    if (!failure.message.empty()) {
        return failure.details.empty() ? failure.message : failure.message + ": " + failure.details;
    }
    if (failure.status == 0) return tx("Sin conexion con el servidor.", "Could not reach the server.");
    return fmt::format("{} (HTTP {})", tx("Error del servidor", "Server error"), failure.status);
}

bool isNotFound(std::string const& response) {
    return parseFailure(response).status == 404;
}

void showOnTop(FLAlertLayer* popup) {
    if (!popup) return;
    int top = 105;
    if (auto* scene = CCDirector::get()->getRunningScene()) {
        for (auto* child : CCArrayExt<CCNode*>(scene->getChildren())) {
            if (child) top = std::max(top, child->getZOrder() + 1);
        }
    }
    popup->m_ZOrder = top;
    popup->show();
    if (auto* parent = popup->getParent()) parent->reorderChild(popup, top);
}

void promptVerification() {
    if (!currentIdentity()) {
        PaimonNotify::show(notLoggedIn(), NotificationIcon::Warning);
        return;
    }
    paimon::modauth::showPanel();
}

CommunityClient& CommunityClient::get() {
    static CommunityClient instance;
    return instance;
}

std::string CommunityClient::backgroundUrl(std::string const& id) {
    return HttpClient::get().getServerURL() + "/community/backgrounds/" + id;
}

void CommunityClient::getJson(std::string const& endpoint, JsonCallback cb) {
    HttpClient::get().get(endpoint, [cb = std::move(cb)](bool success, std::string const& response) {
        handleJson(success, response, cb);
    });
}

void CommunityClient::postJson(std::string const& endpoint, matjson::Value body, JsonCallback cb) {
    auto identity = currentIdentity();
    if (!identity) {
        if (cb) cb(false, matjson::Value(), notLoggedIn());
        return;
    }
    withIdentity(body, *identity);
    HttpClient::get().post(endpoint, body.dump(matjson::NO_INDENTATION),
        [cb = std::move(cb)](bool success, std::string const& response) {
            handleJson(success, response, cb);
        });
}

void CommunityClient::saveSettings(matjson::Value const& payload, int schema, JsonCallback cb) {
    auto body = matjson::makeObject({
        {"schema", schema},
        {"modVersion", Mod::get()->getVersion().toVString()},
        {"payload", payload},
    });
    postJson("/api/community/settings", std::move(body), std::move(cb));
}

void CommunityClient::loadSettings(JsonCallback cb) {
    auto identity = currentIdentity();
    if (!identity) { if (cb) cb(false, matjson::Value(), notLoggedIn()); return; }
    getJson("/api/community/settings?" + identityQuery(*identity), std::move(cb));
}

void CommunityClient::settingsInfo(JsonCallback cb) {
    auto identity = currentIdentity();
    if (!identity) { if (cb) cb(false, matjson::Value(), notLoggedIn()); return; }
    getJson("/api/community/settings/info?" + identityQuery(*identity), std::move(cb));
}

void CommunityClient::deleteSettings(JsonCallback cb) {
    postJson("/api/community/settings/delete", matjson::makeObject({}), std::move(cb));
}

namespace {
bool validItemType(std::string const& type) {
    return type == "presets" || type == "backgrounds" || type == "gradients" || type == "icons";
}
} // namespace

void CommunityClient::listItems(std::string const& type, std::string const& filter, std::string const& sort, int offset, int limit, JsonCallback cb) {
    if (!validItemType(type)) { if (cb) cb(false, matjson::Value(), "Unknown type"); return; }
    std::string endpoint = fmt::format("/api/community/items/{}?offset={}&limit={}&sort={}", type, std::max(0, offset),
        std::clamp(limit, 1, 48), HttpClient::encodeQueryParam(sort.empty() ? "new" : sort));
    if (!filter.empty()) {
        endpoint += (type == "presets" ? "&category=" : "&iconType=") + HttpClient::encodeQueryParam(filter);
    }
    // with account proof the server also returns the viewer's own votes.
    if (auto identity = currentIdentity(); identity && hasAccountProof()) endpoint += "&" + identityQuery(*identity);
    getJson(endpoint, std::move(cb));
}

void CommunityClient::getItem(std::string const& type, std::string const& id, JsonCallback cb) {
    if (!validItemType(type) || !isCommunityId(id)) { if (cb) cb(false, matjson::Value(), tx("ID invalido.", "Invalid ID.")); return; }
    std::string endpoint = fmt::format("/api/community/items/{}/{}", type, id);
    if (auto identity = currentIdentity(); identity && hasAccountProof()) endpoint += "?" + identityQuery(*identity);
    getJson(endpoint, std::move(cb));
}

void CommunityClient::submitItem(std::string const& type, matjson::Value body, JsonCallback cb) {
    if (!validItemType(type) || type == "backgrounds") { if (cb) cb(false, matjson::Value(), "Unknown type"); return; }
    postJson("/api/community/items/" + type, std::move(body), std::move(cb));
}

void CommunityClient::myItems(std::string const& type, JsonCallback cb) {
    auto identity = currentIdentity();
    if (!identity) { if (cb) cb(false, matjson::Value(), notLoggedIn()); return; }
    if (!validItemType(type)) { if (cb) cb(false, matjson::Value(), "Unknown type"); return; }
    getJson(fmt::format("/api/community/items/{}/mine?{}", type, identityQuery(*identity)), std::move(cb));
}

void CommunityClient::deleteItem(std::string const& type, std::string const& id, JsonCallback cb) {
    if (!validItemType(type)) { if (cb) cb(false, matjson::Value(), "Unknown type"); return; }
    postJson(fmt::format("/api/community/items/{}/delete", type), matjson::makeObject({{"id", id}}), std::move(cb));
}

void CommunityClient::vote(std::string const& type, std::string const& id, int value, JsonCallback cb) {
    if (!validItemType(type) || !isCommunityId(id)) { if (cb) cb(false, matjson::Value(), tx("ID invalido.", "Invalid ID.")); return; }
    postJson(fmt::format("/api/community/items/{}/{}/vote", type, id), matjson::makeObject({{"value", std::clamp(value, -1, 1)}}), std::move(cb));
}

void CommunityClient::countDownload(std::string const& type, std::string const& id, JsonCallback cb) {
    if (!validItemType(type) || !isCommunityId(id) || !currentIdentity() || !hasAccountProof()) {
        if (cb) cb(false, matjson::Value(), "");
        return;
    }
    postJson(fmt::format("/api/community/items/{}/{}/download", type, id), matjson::makeObject({}), std::move(cb));
}

void CommunityClient::submitBackground(std::string const& title, std::vector<uint8_t> const& image,
    std::string const& contentType, JsonCallback cb) {
    auto identity = currentIdentity();
    if (!identity) { if (cb) cb(false, matjson::Value(), notLoggedIn()); return; }
    std::vector<std::pair<std::string, std::string>> fields = {
        {"username", identity->username},
        {"accountID", std::to_string(identity->accountID)},
        {"title", title},
    };
    HttpClient::get().uploadMultipart("/api/community/items/backgrounds", fields, "image", "background", image, contentType,
        [cb = std::move(cb)](bool success, std::string const& response) { handleJson(success, response, cb); });
}

void CommunityClient::moderationSummary(JsonCallback cb) {
    auto identity = currentIdentity();
    if (!identity) { if (cb) cb(false, matjson::Value(), notLoggedIn()); return; }
    getJson("/api/community/moderation/summary?" + identityQuery(*identity), std::move(cb));
}

void CommunityClient::moderationQueue(std::string const& type, int offset, JsonCallback cb) {
    auto identity = currentIdentity();
    if (!identity) { if (cb) cb(false, matjson::Value(), notLoggedIn()); return; }
    getJson(fmt::format("/api/community/moderation/queue?type={}&offset={}&limit=4&{}",
        HttpClient::encodeQueryParam(type), std::max(0, offset), identityQuery(*identity)), std::move(cb));
}

void CommunityClient::review(std::string const& type, std::string const& id, bool approve,
    std::string const& reason, JsonCallback cb) {
    auto body = matjson::makeObject({
        {"type", type}, {"id", id}, {"decision", approve ? "approve" : "reject"}, {"reason", reason},
    });
    postJson("/api/community/moderation/review", std::move(body), std::move(cb));
}

void CommunityClient::moderationMedia(std::string const& mediaPath, BytesCallback cb) {
    auto identity = currentIdentity();
    // the queue hands out a fixed path; only that endpoint is ever requested.
    if (!identity || mediaPath.rfind("/api/community/moderation/media?", 0) != 0) {
        if (cb) cb(false, {});
        return;
    }
    HttpClient::get().getBinaryWithAuth(mediaPath + "&" + identityQuery(*identity),
        [cb = std::move(cb)](bool success, std::vector<uint8_t> const& data) {
            if (cb) cb(success && !data.empty(), data);
        });
}

void CommunityClient::uploadAsset(std::string const& kind, std::vector<uint8_t> const& data, JsonCallback cb) {
    auto identity = currentIdentity();
    if (!identity) { if (cb) cb(false, matjson::Value(), notLoggedIn()); return; }
    std::vector<std::pair<std::string, std::string>> fields = {
        {"username", identity->username},
        {"accountID", std::to_string(identity->accountID)},
        {"kind", kind},
    };
    auto contentType = kind == "png" ? "image/png" : "application/xml";
    HttpClient::get().uploadMultipart("/api/community/assets", fields, "file", "asset." + kind, data, contentType,
        [cb = std::move(cb)](bool success, std::string const& response) { handleJson(success, response, cb); });
}

void CommunityClient::publishIcons(matjson::Value body, JsonCallback cb) {
    postJson("/api/community/icons", std::move(body), std::move(cb));
}

void CommunityClient::clearIcons(JsonCallback cb) {
    postJson("/api/community/icons/clear", matjson::makeObject({}), std::move(cb));
}

void CommunityClient::downloadPublic(std::string const& url, BytesCallback cb) {
    HttpClient::get().downloadFromUrlRaw(url,
        [cb = std::move(cb)](bool success, std::vector<uint8_t> const& data, int, int) {
            if (cb) cb(success && !data.empty(), data);
        });
}

} // namespace paimon::community
