#include "ModAuthFlow.hpp"
#include "UnifiedAuthFlow.hpp"

#include "../ui/PaimonPopup.hpp"
#include "../ui/PaimonUI.hpp"
#include "RuntimeLifecycle.hpp"
#include "BanPolicy.hpp"
#include "modules/ModuleRegistry.hpp"
#include "../utils/AccountVerifier.hpp"
#include "../utils/Base64.hpp"
#include "../utils/DynamicPopupRegistry.hpp"
#include "../utils/HttpClient.hpp"
#include "../utils/MainThreadDelay.hpp"
#include "../utils/PaimonNotification.hpp"
#include "../utils/SafeText.hpp"
#include "../utils/WebHelper.hpp"
#include "../features/badges/services/RoleService.hpp"
#include "../features/moderation/ui/VerificationCenterLayer.hpp"
#include "../features/transitions/services/TransitionManager.hpp"
#include "../features/twitch-requests/TwitchRequestManager.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/GJAccountManager.hpp>
#include <Geode/binding/ProfilePage.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/utils/string.hpp>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>

using namespace geode::prelude;

namespace paimon::modauth {
namespace {

using paimon::auth::AuthIdentity;
using paimon::auth::LoginActions;
using paimon::auth::LoginChallenge;
using paimon::auth::LoginCompletion;
using paimon::auth::LoginMode;
using paimon::auth::LoginStage;
using paimon::auth::UnifiedAuthFlow;

constexpr auto kSessionTTL = std::chrono::hours(12);

std::string s_verifiedUser;
std::string s_verifiedCredential;
int s_verifiedAccount = 0;
SessionRoles s_verifiedRoles;
std::chrono::steady_clock::time_point s_verifiedAt;

bool matchesAccount(std::string const& username, int accountID) {
    auto* account = GJAccountManager::get();
    auto* game = GameManager::get();
    if (!account || !game || accountID <= 0 || account->m_accountID != accountID) return false;

    std::string const accountName = account->m_username;
    std::string const playerName = game->m_playerName;
    if (!paimon::text::isSafeAccountName(accountName) ||
        !paimon::text::isSafeAccountName(playerName) ||
        !paimon::ban::sameAccountName(accountName, playerName)) {
        return false;
    }

    return !username.empty() && paimon::text::isSafeAccountName(username) &&
        paimon::ban::sameAccountName(username, playerName);
}

std::string activeCredential() {
    auto& http = HttpClient::get();
    if (auto token = http.getSessionToken(); !token.empty()) return token;
    return http.getModCode();
}

AuthIdentity currentIdentity() {
    auto account = AccountVerifier::get().verify();
    if (!account.loggedIn()) return {};
    return {account.accountID, std::move(account.username)};
}

bool isCurrentIdentity(AuthIdentity const& identity) {
    return !paimon::isRuntimeShuttingDown()
        && UnifiedAuthFlow::sameIdentity(identity, currentIdentity());
}

std::optional<matjson::Value> parseJsonPayload(std::string const& response) {
    auto jsonStart = response.find('{');
    if (jsonStart == std::string::npos) return std::nullopt;
    auto parsed = matjson::parse(response.substr(jsonStart));
    if (!parsed.isOk() || !parsed.unwrap().isObject()) return std::nullopt;
    return parsed.unwrap();
}

std::string stringField(matjson::Value const& value, char const* key) {
    if (!value.contains(key)) return {};
    return value[key].asString().unwrapOr("");
}

AuthIdentity responseIdentity(matjson::Value const& value) {
    auto id = value["accountID"].asInt().unwrapOr(0);
    return {
        id > 0 && id <= std::numeric_limits<int>::max() ? static_cast<int>(id) : 0,
        stringField(value, "username")
    };
}

SessionRoles parseRoles(matjson::Value const& json, bool authenticated) {
    SessionRoles roles;
    roles.authenticated = authenticated;
    if (!authenticated) return roles;

    if (json.contains("roles") && json["roles"].isObject()) {
        auto const& r = json["roles"];
        roles.isAdmin = r["admin"].asBool().unwrapOr(false);
        roles.isModerator = roles.isAdmin || r["moderator"].asBool().unwrapOr(false);
        roles.isVip = roles.isModerator || r["vip"].asBool().unwrapOr(false);
        roles.isHelper = r["helper"].asBool().unwrapOr(false);
        roles.isIdea = r["idea"].asBool().unwrapOr(false);
    } else {
        roles.isAdmin = json["isAdmin"].asBool().unwrapOr(false);
        roles.isModerator = roles.isAdmin || json["isModerator"].asBool().unwrapOr(false);
        roles.isVip = roles.isModerator || json["isVip"].asBool().unwrapOr(false);
        roles.isHelper = json["isHelper"].asBool().unwrapOr(false);
        roles.isIdea = json["isIdea"].asBool().unwrapOr(false);
    }
    roles.primaryRole = stringField(json, "primaryRole");
    if (roles.primaryRole.empty()) {
        roles.primaryRole = roles.isAdmin ? "admin"
            : roles.isModerator ? "moderator"
            : roles.isVip ? "vip"
            : roles.isHelper ? "helper"
            : roles.isIdea ? "idea" : "user";
    }
    if (json.contains("permissions") && json["permissions"].isArray()) {
        if (auto arr = json["permissions"].asArray(); arr.isOk()) {
            for (auto const& item : arr.unwrap()) {
                if (auto p = item.asString(); p.isOk() && !p.unwrap().empty()) {
                    roles.permissions.push_back(p.unwrap());
                }
            }
        }
    }
    return roles;
}

std::string formatRolesSummary(SessionRoles const& roles) {
    if (!roles.authenticated) return "Sin sesion activa";
    std::vector<std::string> badges;
    if (roles.isAdmin) badges.push_back("Admin");
    else if (roles.isModerator) badges.push_back("Moderador");
    if (roles.isVip && !roles.isAdmin && !roles.isModerator) badges.push_back("VIP");
    if (roles.isHelper) badges.push_back("Helper");
    if (roles.isIdea) badges.push_back("Idea");
    if (badges.empty()) return "Conectado (Usuario verificado)";
    std::string out = "Conectado: ";
    for (size_t i = 0; i < badges.size(); ++i) {
        if (i > 0) out += " + ";
        out += badges[i];
    }
    return out;
}

std::string commentKey(int accountID) {
    return fmt::format("auth-v3-temp-comment-{}", accountID);
}

int positiveCommentID(std::string response) {
    auto first = response.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return 0;
    auto last = response.find_last_not_of(" \t\r\n");
    int id = 0;
    auto parsed = std::from_chars(response.data() + first, response.data() + last + 1, id);
    return parsed.ec == std::errc{} && parsed.ptr == response.data() + last + 1 && id > 0 ? id : 0;
}

void postOfficialGD(AuthIdentity const& identity, std::string endpoint,
    std::string fields, std::function<void(bool, std::string)> callback) {
    auto* account = GJAccountManager::get();
    if (!isCurrentIdentity(identity) || !account || account->m_GJP2.empty()) {
        callback(false, {});
        return;
    }
    // GD credentials NEVER leave this fixed official Boomlings origin, even on HTTP redirect.
    std::string body = fmt::format("accountID={}&gjp2={}&secret=Wmfd2893gb7&{}",
        identity.accountID, HttpClient::encodeQueryParamForm(std::string(account->m_GJP2)), fields);
    web::WebRequest request;
    request.header("Content-Type", "application/x-www-form-urlencoded")
        .userAgent("")
        .followRedirects(false)
        .timeout(asp::Duration::fromSecs(15))
        .bodyString(std::move(body));
    WebHelper::dispatch(std::move(request), "POST", "https://www.boomlings.com/database/" + endpoint,
        [callback = std::move(callback)](web::WebResponse response) {
            callback(response.ok(), response.string().unwrapOr(""));
        });
}

std::string mapAuthError(std::string const& code, bool completing) {
    if (code == "BANNED") return "Tu cuenta esta baneada en Paimbnails.";
    if (code == "AUTH_RATE_LIMITED" || code == "MOD_AUTH_RATE_LIMITED" || code == "RATE_LIMITED") {
        return "Demasiados intentos. Espera unos minutos y reintenta.";
    }
    if (code == "AUTH_CHALLENGE_INVALID" || code == "CHALLENGE_REQUIRED") {
        return "El desafio vencio. Inicia uno nuevo.";
    }
    if (code == "AUTH_CHALLENGE_USED" || code == "AUTH_STALE_CHALLENGE" || code == "AUTH_CHALLENGE_CONFLICT") {
        return "Este desafio ya fue usado o reemplazado. Genera uno nuevo.";
    }
    if (code == "ACCOUNT_MISMATCH" || code == "USERNAME_MISMATCH" || code == "ACCOUNT_NOT_FOUND") {
        return "No se pudo confirmar tu cuenta de Geometry Dash.";
    }
    if (code == "GD_COMMENTS_UNAVAILABLE" || code == "VERIFY_FAILED") {
        return "Servidores de GD ocupados. Reintenta en unos segundos.";
    }
    if (code == "AUTH_NOT_CONFIGURED" || code == "MOD_AUTH_NOT_CONFIGURED") {
        return "El servidor aun no tiene configurada la autenticacion segura.";
    }
    if (code == "AUTH_COORDINATOR_UNAVAILABLE" || code == "AUTH_STORAGE_FAILED") {
        return "Servidor de autenticacion temporalmente no disponible.";
    }
    return completing
        ? "No se pudo verificar el comentario. Reintenta o usa el modo manual."
        : "No se pudo iniciar la sesion segura. Reintenta.";
}

std::shared_ptr<UnifiedAuthFlow> createUnifiedFlow() {
    static std::weak_ptr<UnifiedAuthFlow> active;
    static AuthIdentity activeIdentity;
    auto identity = currentIdentity();
    if (identity.accountID <= 0 || identity.username.empty()) return {};
    if (auto flow = active.lock(); flow && UnifiedAuthFlow::sameIdentity(identity, activeIdentity)) {
        return flow;
    }

    LoginActions actions;
    actions.isCurrentAccount = [identity] { return isCurrentIdentity(identity); };
    actions.startChallenge = [identity](auto callback) {
        HttpClient::get().startUnifiedAuth(identity.username, identity.accountID,
            [callback = std::move(callback)](bool ok, std::string const& body) {
                auto parsed = parseJsonPayload(body);
                if (!ok || !parsed) {
                    auto code = parsed ? stringField(*parsed, "code") : std::string();
                    callback({false, {}, {}, {}, mapAuthError(code, false)});
                    return;
                }
                auto const& json = *parsed;
                auto profileCode = stringField(json, "profileCode");
                if (profileCode.empty()) profileCode = stringField(json, "code");
                callback({
                    json["success"].asBool().unwrapOr(false),
                    responseIdentity(json),
                    stringField(json, "challengeToken"),
                    std::move(profileCode),
                    {}
                });
            });
    };
    actions.publishComment = [identity](std::string const& code, auto callback) {
        if (!UnifiedAuthFlow::validProfileCode(code)) {
            callback(false, 0, "Codigo de verificacion invalido.");
            return;
        }
        std::vector<uint8_t> bytes(code.begin(), code.end());
        auto encoded = paimon::base64Encode(bytes);
        std::replace(encoded.begin(), encoded.end(), '+', '-');
        std::replace(encoded.begin(), encoded.end(), '/', '_');
        auto fields = "comment=" + HttpClient::encodeQueryParamForm(encoded)
            + "&cType=1&userName=" + HttpClient::encodeQueryParamForm(identity.username);
        postOfficialGD(identity, "uploadGJAccComment20.php", std::move(fields),
            [callback = std::move(callback)](bool ok, std::string body) {
                auto id = ok ? positiveCommentID(std::move(body)) : 0;
                callback(id > 0, id, id > 0 ? "" : "GD no confirmo el comentario. Puedes usar el modo manual.");
            });
    };
    actions.completeChallenge = [](std::string const& challengeToken, auto callback) {
        HttpClient::get().completeUnifiedAuth(challengeToken,
            [callback = std::move(callback)](bool ok, std::string const& response) {
                auto parsed = parseJsonPayload(response);
                if (!parsed) {
                    callback({false, false, {}, {}, 0, {}, "Respuesta invalida del servidor. Reintenta."});
                    return;
                }
                auto const& json = *parsed;
                auto errCode = stringField(json, "code");
                bool const pending = errCode == "PROFILE_CODE_NOT_FOUND";
                if (!ok || !json["success"].asBool().unwrapOr(false)) {
                    callback({
                        false,
                        pending,
                        {},
                        {},
                        0,
                        {},
                        pending
                            ? "El comentario aun no aparece en tu perfil de GD."
                            : mapAuthError(errCode, true)
                    });
                    return;
                }
                auto sessionToken = stringField(json, "sessionToken");
                if (sessionToken.empty()) sessionToken = stringField(json, "token");
                auto expiresAt = json["expiresAt"].asInt().unwrapOr(0);
                auto roles = parseRoles(json, true);
                callback({true, false, responseIdentity(json), std::move(sessionToken), expiresAt, std::move(roles), {}});
            });
    };
    actions.deleteComment = [identity](int id, auto callback) {
        if (id <= 0) {
            callback(false, "No hay comentario temporal identificado.");
            return;
        }
        postOfficialGD(identity, "deleteGJAccComment20.php",
            fmt::format("commentID={}&targetAccountID={}&cType=1", id, identity.accountID),
            [callback = std::move(callback)](bool ok, std::string body) {
                bool const deleted = ok && positiveCommentID(std::move(body)) == 1;
                callback(deleted, deleted ? "" : "Reintenta el borrado o elimina el comentario desde tu perfil.");
            });
    };
    actions.storeSession = [identity](std::string const& token, AuthIdentity const& verifiedId, int64_t expiresAt, SessionRoles const& roles) {
        if (!isCurrentIdentity(identity)) return;
        HttpClient::get().setSessionToken(token, verifiedId.username, verifiedId.accountID, expiresAt);
        setVerifiedSession(verifiedId.username, verifiedId.accountID, roles);
        Mod::get()->setSavedValue<bool>("is-verified-vip", isVerifiedVip());

        paimon::roles::UserRoles badgeRoles;
        badgeRoles.admin = roles.isAdmin;
        badgeRoles.mod = roles.isModerator || roles.isAdmin;
        badgeRoles.vip = roles.isVip || badgeRoles.mod;
        badgeRoles.helper = roles.isHelper;
        badgeRoles.idea = roles.isIdea;
        paimon::roles::RoleService::get().update(verifiedId.username, badgeRoles);
        paimon::requestDeferredModSave();
    };
    actions.rememberComment = [identity](int id) {
        Mod::get()->setSavedValue<int>(commentKey(identity.accountID), id);
        paimon::requestDeferredModSave();
    };
    actions.onLoggedIn = [] {
        paimon::twitch::TwitchRequestManager::get().restartWebRequests();
    };
    actions.schedule = [](std::function<void()> callback) {
        paimon::scheduleMainThreadDelay(3.f, [callback = std::move(callback)] { callback(); });
    };

    auto pending = Mod::get()->getSavedValue<int>(commentKey(identity.accountID), 0);
    auto flow = std::make_shared<UnifiedAuthFlow>(identity, std::move(actions), std::max(0, pending));
    activeIdentity = identity;
    active = flow;
    return flow;
}

CCMenuItemSpriteExtra* addBtn(CCMenu* menu, char const* title, CCPoint pos,
    std::function<void()> action, paimon::ui::Btn skin, float scale = .46f, float width = 0.f) {
    auto* sprite = paimon::ui::makeButtonSprite(title, skin, width, scale, "bigFont.fnt");
    if (!sprite) return nullptr;
    auto* button = CCMenuItemExt::createSpriteExtra(sprite,
        [action = std::move(action)](CCMenuItemSpriteExtra*) { action(); });
    button->setPosition(pos);
    menu->addChild(button);
    return button;
}

void setBtnEnabled(CCMenuItemSpriteExtra* button, bool enabled) {
    if (!button) return;
    button->setEnabled(enabled);
    button->setOpacity(enabled ? 255 : 105);
}

class AccountSessionPopup final : public PaimonPopup {
    std::shared_ptr<UnifiedAuthFlow> m_flow;
    CCLabelBMFont* m_roleBadge = nullptr;
    CCLabelBMFont* m_status = nullptr;
    CCLabelBMFont* m_code = nullptr;
    CCMenuItemSpriteExtra* m_automatic = nullptr;
    CCMenuItemSpriteExtra* m_manual = nullptr;
    CCMenuItemSpriteExtra* m_copy = nullptr;
    CCMenuItemSpriteExtra* m_confirm = nullptr;
    CCMenuItemSpriteExtra* m_cleanup = nullptr;
    CCMenuItemSpriteExtra* m_forget = nullptr;
    CCMenuItemSpriteExtra* m_refresh = nullptr;
    CCMenuItemSpriteExtra* m_modCenter = nullptr;
    CCMenuItemSpriteExtra* m_logout = nullptr;
    bool m_refreshing = false;
    bool m_loggingOut = false;
    std::string m_lastStatus;
    std::string m_lastCode;
    std::string m_lastRole;

    ~AccountSessionPopup() override {
        if (m_flow) m_flow->cancel();
    }

    bool init() override {
        m_flow = createUnifiedFlow();
        namespace ui = paimon::ui;
        constexpr float W = 404.f, H = 296.f, cx = W / 2.f;
        if (!m_flow || !PaimonPopup::init(W, H)) return false;

        setTitle("Cuenta Paimbnails", "goldFont.fnt", .62f, 18.f);
        addCorners();
        addInfoButton("Inicio de sesion unificado",
            "Una sola sesion segura para <cg>todo Paimbnails</c> (Paimon Base, chat, peticiones, perfil y moderacion).\n\n"
            "<cg>Automatico (1 clic):</c> publica un codigo temporal en tu perfil de GD, el servidor comprueba que la cuenta es tuya, "
            "asigna tus <cy>permisos reales</c> (Usuario, VIP, Helper, Idea, Moderador o Admin) y borra solo ese comentario.\n\n"
            "Tu credencial de GD <cy>solo se envia a Geometry Dash</c>; nunca sale hacia Paimbnails.\n\n"
            "<cj>Manual:</c> copias el codigo, lo publicas en tu perfil y pulsas Confirmar.",
            Anchor::TopRight, {-16.f, -16.f});
        paimon::markDynamicPopup(this);

        auto* divider = ui::makeDivider(344.f);
        divider->setPosition({cx, H - 34.f});
        m_mainLayer->addChild(divider);

        // Account identity + live granted role summary
        auto account = AccountVerifier::get().verify();
        auto* headInset = ui::makeInset({372.f, 44.f}, 115);
        headInset->setPosition({16.f, H - 86.f});
        m_mainLayer->addChild(headInset);

        auto idStr = fmt::format("{}  #{}", account.username, account.accountID);
        auto* identityLbl = ui::makeLabel(idStr.c_str(), 350.f, .44f, ui::palette::gold);
        identityLbl->setPosition({cx, H - 55.f});
        m_mainLayer->addChild(identityLbl);

        m_roleBadge = CCLabelBMFont::create("Comprobando sesion...", "bigFont.fnt");
        m_roleBadge->setScale(.34f);
        m_roleBadge->setPosition({cx, H - 74.f});
        m_roleBadge->setID("moderation-session-status"_spr);
        m_mainLayer->addChild(m_roleBadge);

        auto* menu = CCMenu::create();
        menu->setPosition({0.f, 0.f});
        menu->setTouchPriority(CCDirector::get()->getTouchDispatcher()->getTargetPrio() - 2);
        m_mainLayer->addChild(menu, 5);

        // Easy login cards: Automatic (1-click) and Manual
        auto addCard = [&](float x, ccColor3B tint, char const* hint) {
            auto* card = ui::makeInset({180.f, 54.f}, 92, tint);
            card->setPosition({x, 148.f});
            m_mainLayer->addChild(card);
            auto* text = ui::makeText(hint, 162.f, .31f, ui::palette::muted, kCCTextAlignmentCenter);
            text->setPosition({x + 90.f, 161.f});
            m_mainLayer->addChild(text);
        };
        addCard(16.f, {20, 75, 28}, "1 clic: publica y borra el codigo.");
        addCard(208.f, {15, 62, 78}, "Copias, publicas y confirmas.");

        m_automatic = addBtn(menu, "Automatico", {106.f, 184.f}, [this] { onAutomatic(); }, ui::Btn::Green, .48f);
        m_manual = addBtn(menu, "Manual", {298.f, 184.f}, [this] {
            m_flow->start(LoginMode::Manual);
            refreshUI();
        }, ui::Btn::Cyan, .48f);

        // Status + challenge code panel
        auto* statusBox = ui::makeInset({372.f, 46.f}, 122);
        statusBox->setPosition({16.f, 94.f});
        m_mainLayer->addChild(statusBox);

        m_status = CCLabelBMFont::create("", "chatFont.fnt", 354.f / .39f, kCCTextAlignmentCenter);
        m_status->setScale(.39f);
        m_status->setPosition({cx, 117.f});
        m_mainLayer->addChild(m_status);

        m_code = CCLabelBMFont::create("", "goldFont.fnt");
        m_code->setScale(.46f);
        m_code->setPosition({cx, 104.f});
        m_mainLayer->addChild(m_code);

        // Row 1: Challenge actions
        m_copy = addBtn(menu, "Copiar codigo", {76.f, 72.f}, [this] {
            PlatformToolbox::copyToClipboard(m_flow->code());
            PaimonNotify::show("Codigo copiado. Publicalo en tu perfil de GD.", NotificationIcon::Info);
        }, ui::Btn::Gray, .38f);
        addBtn(menu, "Mi perfil", {cx, 72.f}, [accountID = account.accountID] {
            if (auto* profile = ProfilePage::create(accountID, true)) profile->show();
        }, ui::Btn::Gray, .38f);
        m_confirm = addBtn(menu, "Confirmar", {328.f, 72.f}, [this] {
            m_flow->confirm();
            refreshUI();
        }, ui::Btn::Green, .38f);

        // Cleanup row (visible only when a temporary auto comment needs deletion)
        m_cleanup = addBtn(menu, "Borrar temporal", {134.f, 47.f}, [this] {
            m_flow->retryCleanup();
            refreshUI();
        }, ui::Btn::Cyan, .34f);
        m_forget = addBtn(menu, "Ya lo borre", {270.f, 47.f}, [this] { onForgetComment(); }, ui::Btn::Gray, .34f);

        // Bottom row: Session & Permissions management
        m_refresh = addBtn(menu, "Permisos", {76.f, 22.f}, [this] { onRefreshSession(); }, ui::Btn::Cyan, .38f);
        m_modCenter = addBtn(menu, "Moderacion", {cx, 22.f}, [this] { onOpenModCenter(); }, ui::Btn::Green, .38f);
        m_logout = addBtn(menu, "Cerrar sesion", {328.f, 22.f}, [this] { onLogout(); }, ui::Btn::Red, .38f);

        refreshUI();
        if (!activeCredential().empty()) {
            onRefreshSession(!isAuthenticated());
        }
        scheduleUpdate();
        return true;
    }

    void onAutomatic() {
        if (m_flow->busy() || m_flow->commentID() > 0) return;
        WeakRef<AccountSessionPopup> self = this;
        createQuickPopup("Inicio de sesion automatico",
            "Al pulsar <cg>Autorizar</c>, el mod publicara un codigo temporal en <cy>tu perfil de GD</c>, "
            "iniciara sesion segura con tus permisos reales y borrara solo ese comentario.\n\n"
            "Tu credencial de GD <cy>nunca</c> se envia a Paimbnails.",
            "Cancelar", "Autorizar", [self](auto*, bool confirmed) {
                if (auto popup = self.lock(); popup && popup->getParent() && confirmed) {
                    popup->m_flow->start(LoginMode::Automatic);
                    popup->refreshUI();
                }
            });
    }

    void onForgetComment() {
        WeakRef<AccountSessionPopup> self = this;
        createQuickPopup("Comentario eliminado manualmente",
            "Confirma que ya borraste el comentario temporal desde tu perfil de GD.",
            "Cancelar", "Confirmar", [self](auto*, bool confirmed) {
                if (auto popup = self.lock(); popup && popup->getParent() && confirmed) {
                    popup->m_flow->acknowledgeManualCleanup();
                    popup->refreshUI();
                }
            });
    }

    void onRefreshSession(bool notifyOnSuccess = true) {
        if (m_refreshing || m_loggingOut) return;
        m_refreshing = true;
        refreshUI();
        WeakRef<AccountSessionPopup> self = this;
        refreshSession([self, notifyOnSuccess](SessionRoles const& roles) {
            auto popup = self.lock();
            if (!popup) return;
            popup->m_refreshing = false;
            popup->refreshUI();
            if (roles.authenticated) {
                if (notifyOnSuccess) {
                    PaimonNotify::show(formatRolesSummary(roles), NotificationIcon::Success);
                }
            } else if (!activeCredential().empty()) {
                PaimonNotify::show("No se pudo confirmar la sesion en el servidor.", NotificationIcon::Warning);
            }
        });
    }

    void onOpenModCenter() {
        if (!isVerified()) {
            onRefreshSession();
            return;
        }
        auto scene = VerificationCenterLayer::scene();
        if (!scene) return;
        onClose(nullptr);
        TransitionManager::get().pushScene(scene);
    }

    void onLogout() {
        if (m_loggingOut) return;
        m_loggingOut = true;
        if (m_flow) m_flow->cancel();
        refreshUI();
        WeakRef<AccountSessionPopup> self = this;
        logout([self](bool) {
            auto popup = self.lock();
            if (!popup) return;
            popup->m_loggingOut = false;
            popup->refreshUI();
            PaimonNotify::show("Sesion cerrada y revocada.", NotificationIcon::Info);
        });
    }

    void refreshUI() {
        auto roles = currentRoles();
        bool const hasCred = !activeCredential().empty();
        std::string roleText = m_loggingOut ? "Cerrando sesion..."
            : m_refreshing ? "Consultando permisos en el servidor..."
            : roles.authenticated ? formatRolesSummary(roles)
            : hasCred ? "Sesion guardada (pulsa Permisos para verificar)"
            : "Sin iniciar sesion";
        if (m_lastRole != roleText) {
            m_lastRole = roleText;
            m_roleBadge->setString(roleText.c_str());
            m_roleBadge->limitLabelWidth(356.f, .34f, .2f);
            m_roleBadge->setColor(roles.authenticated
                ? (roles.isAdmin ? ccc3(255, 130, 130)
                    : roles.isModerator ? ccc3(110, 240, 150)
                    : roles.isVip ? ccc3(255, 215, 100)
                    : ccc3(120, 225, 255))
                : ccc3(255, 195, 110));
        }

        if (m_lastStatus != m_flow->status()) {
            m_lastStatus = m_flow->status();
            m_status->setString(m_lastStatus.c_str());
        }
        if (m_lastCode != m_flow->code()) {
            m_lastCode = m_flow->code();
            m_code->setString(m_lastCode.c_str());
            m_status->setPositionY(m_lastCode.empty() ? 117.f : 124.f);
        }

        bool const pendingCleanup = m_flow->commentID() > 0;
        bool const busy = m_flow->busy() || m_loggingOut;
        setBtnEnabled(m_automatic, !busy && !pendingCleanup);
        setBtnEnabled(m_manual, !busy && !pendingCleanup);
        setBtnEnabled(m_copy, !m_flow->code().empty());
        setBtnEnabled(m_confirm, m_flow->stage() == LoginStage::Waiting && !busy);
        if (m_cleanup) {
            m_cleanup->setVisible(pendingCleanup);
            setBtnEnabled(m_cleanup, !busy);
        }
        if (m_forget) {
            m_forget->setVisible(pendingCleanup);
            setBtnEnabled(m_forget, !busy);
        }
        setBtnEnabled(m_refresh, !m_refreshing && !m_loggingOut);
        if (m_modCenter) {
            bool const canMod = roles.hasModAccess();
            m_modCenter->setVisible(canMod);
            setBtnEnabled(m_modCenter, canMod && !m_loggingOut);
        }
        setBtnEnabled(m_logout, hasCred && !m_loggingOut);
    }

    void update(float) override { refreshUI(); }

    void onClose(CCObject* sender) override {
        if (m_flow) m_flow->cancel();
        PaimonPopup::onClose(sender);
    }

public:
    static AccountSessionPopup* create() {
        auto* ret = new AccountSessionPopup();
        if (ret->init()) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }
};

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

} // namespace

void startOrComplete() {
    showPanel();
}

void clearVerifiedSession() {
    s_verifiedAccount = 0;
    s_verifiedUser.clear();
    s_verifiedCredential.clear();
    s_verifiedRoles = {};
    s_verifiedAt = {};
    Mod::get()->setSavedValue("is-verified-moderator", false);
    Mod::get()->setSavedValue("is-verified-admin", false);
    Mod::get()->setSavedValue("is-verified-vip", false);
}

void setVerifiedSession(std::string const& username, int accountID, SessionRoles const& roles) {
    clearVerifiedSession();
    if (!roles.authenticated || !matchesAccount(username, accountID)) return;
    auto const cred = activeCredential();
    if (cred.empty()) return;
    s_verifiedUser = username;
    s_verifiedAccount = accountID;
    s_verifiedCredential = cred;
    s_verifiedRoles = roles;
    s_verifiedRoles.authenticated = true;
    if (s_verifiedRoles.isAdmin) s_verifiedRoles.isModerator = true;
    if (s_verifiedRoles.isModerator) s_verifiedRoles.isVip = true;
    s_verifiedAt = std::chrono::steady_clock::now();
}

void setVerifiedSession(std::string const& username, int accountID, bool moderator, bool admin, bool vip) {
    SessionRoles roles;
    roles.authenticated = (moderator || admin || vip) && !activeCredential().empty();
    roles.isAdmin = admin;
    roles.isModerator = moderator || admin;
    roles.isVip = vip || roles.isModerator;
    roles.primaryRole = admin ? "admin" : roles.isModerator ? "moderator" : roles.isVip ? "vip" : "user";
    setVerifiedSession(username, accountID, roles);
}

void setVerifiedSession(std::string const& username, int accountID, bool moderator, bool admin) {
    setVerifiedSession(username, accountID, moderator, admin, false);
}

bool isAuthenticated() {
    auto const cred = activeCredential();
    return s_verifiedRoles.authenticated
        && matchesAccount(s_verifiedUser, s_verifiedAccount)
        && !s_verifiedCredential.empty()
        && s_verifiedCredential == cred
        && std::chrono::steady_clock::now() - s_verifiedAt < kSessionTTL;
}

bool isVerified(bool admin) {
    if (!isAuthenticated()) return false;
    return admin ? s_verifiedRoles.isAdmin : (s_verifiedRoles.isModerator || s_verifiedRoles.isAdmin);
}

bool isVerifiedVip() {
    return isAuthenticated() && s_verifiedRoles.isVip;
}

bool isVerifiedHelper() {
    return isAuthenticated() && s_verifiedRoles.isHelper;
}

bool isVerifiedIdea() {
    return isAuthenticated() && s_verifiedRoles.isIdea;
}

SessionRoles currentRoles() {
    if (!isAuthenticated()) return {};
    return s_verifiedRoles;
}

bool hasPermission(std::string_view permission) {
    return isAuthenticated() && s_verifiedRoles.hasPermission(permission);
}

std::string verifiedUsername() {
    return isVerified() ? s_verifiedUser : std::string();
}

int verifiedAccountID() {
    return isVerified() ? s_verifiedAccount : 0;
}

void refreshSession(std::function<void(SessionRoles const&)> callback) {
    auto identity = currentIdentity();
    if (identity.accountID <= 0 || identity.username.empty() || activeCredential().empty()) {
        clearVerifiedSession();
        if (callback) callback({});
        return;
    }
    HttpClient::get().fetchAuthSession([identity, callback = std::move(callback)](bool ok, std::string const& response) {
        queueInMainThread([identity, callback = std::move(callback), ok, response]() mutable {
            if (paimon::isRuntimeShuttingDown()) return;
            if (!isCurrentIdentity(identity)) {
                if (callback) callback({});
                return;
            }
            auto parsed = parseJsonPayload(response);
            if (!ok || !parsed || !(*parsed)["authenticated"].asBool().unwrapOr(false)) {
                auto errCode = parsed ? stringField(*parsed, "code") : std::string();
                if (errCode == "SESSION_REVOKED" || errCode == "SESSION_EXPIRED" ||
                    errCode == "INVALID_SESSION" || errCode == "SESSION_NOT_FOUND" ||
                    errCode == "BANNED" || errCode == "IDENTITY_MISMATCH") {
                    HttpClient::get().clearAllAuthTokens();
                } else {
                    // Transient network/5xx error: keep saved token and existing in-memory session intact
                    log::warn("[ModAuth] Session refresh transient failure: {}", response);
                }
                if (callback) callback(currentRoles());
                return;
            }
            auto const& json = *parsed;
            auto respId = responseIdentity(json);
            if (!UnifiedAuthFlow::sameIdentity(identity, respId)) {
                clearVerifiedSession();
                if (callback) callback({});
                return;
            }
            auto roles = parseRoles(json, true);
            setVerifiedSession(respId.username, respId.accountID, roles);
            Mod::get()->setSavedValue<bool>("is-verified-vip", isVerifiedVip());
            paimon::roles::UserRoles badgeRoles;
            badgeRoles.admin = roles.isAdmin;
            badgeRoles.mod = roles.isModerator || roles.isAdmin;
            badgeRoles.vip = roles.isVip || badgeRoles.mod;
            badgeRoles.helper = roles.isHelper;
            badgeRoles.idea = roles.isIdea;
            paimon::roles::RoleService::get().update(respId.username, badgeRoles);
            if (callback) callback(roles);
        });
    });
}

void logout(std::function<void(bool)> callback) {
    HttpClient::get().logoutAuthSession([callback = std::move(callback)](bool ok, std::string const&) {
        queueInMainThread([callback = std::move(callback), ok]() mutable {
            if (paimon::isRuntimeShuttingDown()) return;
            HttpClient::get().cleanTasks(false);
            HttpClient::get().cleanTasks(true);
            paimon::requestDeferredModSave();
            if (callback) callback(ok);
        });
    });
}

FLAlertLayer* createUnifiedPanel() {
    if (!paimon::modules::isEnabled("paimbnails.modauth.system")) {
        PaimonNotify::show("Activa la autenticacion en Modulos.", NotificationIcon::Warning);
        return nullptr;
    }
    auto identity = currentIdentity();
    if (identity.accountID <= 0 || identity.username.empty()) {
        PaimonNotify::show("Necesitas iniciar sesion en Geometry Dash.", NotificationIcon::Warning);
        return nullptr;
    }
    return AccountSessionPopup::create();
}

void showPanel() {
    if (auto* panel = createUnifiedPanel()) {
        showOnTop(panel);
    }
}

} // namespace paimon::modauth
