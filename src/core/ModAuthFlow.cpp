#include "../ui/PaimonPopup.hpp"
#include "../ui/PaimonUI.hpp"
#include "ModAuthFlow.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/utils/string.hpp>

#include "RuntimeLifecycle.hpp"
#include "modules/ModuleRegistry.hpp"
#include "../utils/HttpClient.hpp"
#include "../features/moderation/ui/VerificationCenterLayer.hpp"
#include "../features/transitions/services/TransitionManager.hpp"

#include "../utils/PaimonNotification.hpp"

#include <chrono>
#include <cstdint>
#include <optional>

using namespace geode::prelude;

namespace paimon::modauth {
namespace {

constexpr auto kChallengeToken = "mod-auth-challenge-token";
constexpr auto kChallengeCode = "mod-auth-challenge-code";
constexpr auto kChallengeUser = "mod-auth-challenge-user";
constexpr auto kChallengeAccount = "mod-auth-challenge-account";
constexpr auto kCredentialExpiresAt = "mod-code-expires-at";

std::chrono::steady_clock::time_point s_requestStarted;
uint64_t s_requestGeneration = 0;

std::string s_verifiedUser;
std::string s_verifiedCredential;
int s_verifiedAccount = 0;
bool s_verifiedMod = false;
bool s_verifiedAdmin = false;
std::chrono::steady_clock::time_point s_verifiedAt;

bool matchesAccount(std::string const& username, int accountID) {
    auto* account = GJAccountManager::get();
    auto* game = GameManager::get();
    return account && game && accountID > 0 && account->m_accountID == accountID &&
        geode::utils::string::toLower(game->m_playerName) == geode::utils::string::toLower(username);
}

bool requestInFlight() {
    if (s_requestStarted == std::chrono::steady_clock::time_point()) return false;
    return std::chrono::steady_clock::now() - s_requestStarted < std::chrono::seconds(30);
}

uint64_t beginRequest() {
    s_requestStarted = std::chrono::steady_clock::now();
    return ++s_requestGeneration;
}

bool finishRequest(uint64_t generation) {
    if (generation != s_requestGeneration) return false;
    s_requestStarted = {};
    return true;
}

std::optional<matjson::Value> parseResponse(std::string const& response) {
    auto jsonStart = response.find('{');
    if (jsonStart == std::string::npos) return std::nullopt;

    auto parsed = matjson::parse(response.substr(jsonStart));
    if (!parsed.isOk()) return std::nullopt;
    if (!parsed.unwrap().isObject()) return std::nullopt;
    return parsed.unwrap();
}

std::string stringField(matjson::Value const& value, char const* key) {
    if (!value.contains(key)) return {};
    return value[key].asString().unwrapOr("");
}

void clearChallenge() {
    auto* mod = Mod::get();
    mod->setSavedValue(kChallengeToken, std::string());
    mod->setSavedValue(kChallengeCode, std::string());
    mod->setSavedValue(kChallengeUser, std::string());
    mod->setSavedValue<int64_t>(kChallengeAccount, 0);
}

void showInstructions(std::string const& code) {
    PlatformToolbox::copyToClipboard(code);
    FLAlertLayer::create(
        "Mod Code seguro",
        fmt::format(
            "Se copio <cy>{}</c> al portapapeles.\n\n"
            "Publicalo como comentario en tu perfil de Geometry Dash y "
            "pulsa <cg>Conectar / Confirmar</c> en el panel de moderacion. "
            "Este codigo es publico; tu credencial de acceso nunca se copia.",
            code
        ),
        "OK"
    )->show();
}

void showRequestError(std::string const& response, bool completing) {
    auto parsed = parseResponse(response);
    auto code = parsed ? stringField(*parsed, "code") : std::string();

    if (code == "PROFILE_CODE_NOT_FOUND") {
        auto profileCode = Mod::get()->getSavedValue<std::string>(kChallengeCode, "");
        if (!profileCode.empty()) showInstructions(profileCode);
        PaimonNotify::create("El comentario aun no aparece en tu perfil.", NotificationIcon::Warning)->show();
        return;
    }
    if (code == "MOD_AUTH_CHALLENGE_INVALID" || code == "CHALLENGE_REQUIRED") {
        clearChallenge();
        PaimonNotify::create("El desafio vencio. Pulsa otra vez para generar uno nuevo.", NotificationIcon::Warning)->show();
        return;
    }
    if (code == "MOD_AUTH_STALE_CHALLENGE" || code == "MOD_AUTH_CHALLENGE_CONFLICT") {
        clearChallenge();
        PaimonNotify::create("Ya existe un desafio mas nuevo. Pulsa otra vez para continuar.", NotificationIcon::Warning)->show();
        return;
    }
    if (code == "MOD_AUTH_RATE_LIMITED") {
        PaimonNotify::create("Demasiados intentos. Espera unos minutos y reintenta.", NotificationIcon::Warning)->show();
        return;
    }
    if (code == "MOD_ROLE_REQUIRED") {
        clearChallenge();
        PaimonNotify::create("Tu cuenta no tiene permisos de mod/admin.", NotificationIcon::Error)->show();
        return;
    }
    if (code == "ACCOUNT_MISMATCH") {
        clearChallenge();
        PaimonNotify::create("La cuenta de Geometry Dash no coincide.", NotificationIcon::Error)->show();
        return;
    }
    if (code == "GD_COMMENTS_UNAVAILABLE") {
        PaimonNotify::create("Los comentarios de GD no estan disponibles. Reintenta en unos minutos.", NotificationIcon::Warning)->show();
        return;
    }
    if (code == "MOD_AUTH_NOT_CONFIGURED") {
        PaimonNotify::create("El servidor aun no tiene activado el Mod Code seguro.", NotificationIcon::Error)->show();
        return;
    }
    if (code == "MOD_AUTH_COORDINATOR_UNAVAILABLE" || code == "MOD_AUTH_STORAGE_FAILED") {
        PaimonNotify::create("El servidor de autenticacion no esta disponible. Reintenta luego.", NotificationIcon::Warning)->show();
        return;
    }

    auto message = completing ? "No se pudo verificar el comentario." : "No se pudo iniciar la verificacion segura.";
    PaimonNotify::create(message, NotificationIcon::Error)->show();
}

void begin(std::string const& username, int accountID) {
    auto generation = beginRequest();
    PaimonNotify::create("Generando desafio seguro...", NotificationIcon::Info)->show();

    HttpClient::get().startModCodeSetup(username, accountID, [username, accountID, generation](bool ok, std::string const& response) {
        queueInMainThread([username, accountID, generation, ok, response] {
            if (!finishRequest(generation)) return;
            if (paimon::isRuntimeShuttingDown() || !matchesAccount(username, accountID)) return;

            auto parsed = parseResponse(response);
            if (!ok || !parsed) {
                showRequestError(response, false);
                return;
            }

            auto token = stringField(*parsed, "challengeToken");
            auto profileCode = stringField(*parsed, "profileCode");
            if (token.empty() || profileCode.empty()) {
                PaimonNotify::create("El servidor devolvio un desafio incompleto.", NotificationIcon::Error)->show();
                return;
            }

            auto* mod = Mod::get();
            mod->setSavedValue(kChallengeToken, token);
            mod->setSavedValue(kChallengeCode, profileCode);
            mod->setSavedValue(kChallengeUser, username);
            mod->setSavedValue<int64_t>(kChallengeAccount, accountID);
            showInstructions(profileCode);
        });
    });
}

void complete(std::string const& token, std::string const& username, int accountID) {
    auto generation = beginRequest();
    PaimonNotify::create("Verificando el comentario...", NotificationIcon::Info)->show();
    HttpClient::get().completeModCodeSetup(token, [generation, username, accountID](bool ok, std::string const& response) {
        queueInMainThread([generation, username, accountID, ok, response] {
            if (!finishRequest(generation) || paimon::isRuntimeShuttingDown() ||
                !matchesAccount(username, accountID)) return;
            auto parsed = parseResponse(response);
            if (!ok || !parsed) {
                showRequestError(response, true);
                return;
            }
            auto credential = stringField(*parsed, "modCode");
            auto expiresAt = (*parsed)["expiresAt"].asInt().unwrapOr(0);
            auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            if (!(*parsed)["success"].asBool().unwrapOr(false) || credential.size() != 50 ||
                !credential.starts_with("pmc_v2_") || expiresAt <= now) {
                PaimonNotify::create("Respuesta de autenticacion invalida.", NotificationIcon::Error)->show();
                return;
            }
            HttpClient::get().setModCode(credential);
            auto* mod = Mod::get();
            mod->setSavedValue<int64_t>(kCredentialExpiresAt, expiresAt);
            mod->setSavedValue<int64_t>("mod-credential-account", accountID);
            mod->setSavedValue("mod-credential-user", username);
            clearChallenge();
            HttpClient::get().checkModeratorAccount(username, accountID, [](bool isMod, bool isAdmin) {
                PaimonNotify::create(isMod || isAdmin ? "Cuenta conectada y permisos verificados." :
                    "Credencial guardada. Vuelve a verificar el estado.",
                    isMod || isAdmin ? NotificationIcon::Success : NotificationIcon::Warning)->show();
            });
        });
    });
}

class ModerationPanel : public PaimonPopup {
    CCLabelBMFont* m_status = nullptr;
    CCMenuItemSpriteExtra* m_verify = nullptr;
    bool m_checking = false;

    bool init() override {
        if (!PaimonPopup::init(390.f, 270.f)) return false;
        setTitle("Moderacion");
        addInfoButton("Mod Code seguro",
            "El <cy>Mod Code</c> conecta tu cuenta de Geometry Dash sin exponer tu credencial.\n\n"
            "1. Pulsa <cg>Conectar / Confirmar</c> para generar un codigo.\n"
            "2. Publicalo como comentario en tu perfil de GD.\n"
            "3. Vuelve y confirma. Usa <cy>Verificar estado</c> para refrescar tus permisos.");
        auto size = m_mainLayer->getContentSize();
        auto* game = GameManager::get();
        auto* account = GJAccountManager::get();

        auto* headInset = paimon::ui::makeInset({size.width - 40.f, 66.f});
        headInset->setPosition({20.f, 170.f});
        m_mainLayer->addChild(headInset);

        auto identity = fmt::format("{}  #{}", game->m_playerName, account->m_accountID);
        auto label = CCLabelBMFont::create(identity.c_str(), "goldFont.fnt");
        label->limitLabelWidth(340.f, 0.6f, 0.25f);
        label->setPosition({size.width / 2, 218.f});
        m_mainLayer->addChild(label);
        m_status = CCLabelBMFont::create("Estado sin verificar", "bigFont.fnt");
        m_status->setScale(0.45f);
        m_status->setPosition({size.width / 2, 184.f});
        m_status->setID("moderation-session-status"_spr);
        m_mainLayer->addChild(m_status);
        auto menu = CCMenu::create();
        menu->setPosition({0, 0});
        m_mainLayer->addChild(menu);
        auto button = [&](char const* text, float y, char const* texture, SEL_MenuHandler selector) {
            auto sprite = ButtonSprite::create(text, 260, true, "bigFont.fnt", texture, 30.f, 0.5f);
            auto item = CCMenuItemSpriteExtra::create(sprite, this, selector);
            item->setPosition({size.width / 2, y});
            menu->addChild(item);
            return item;
        };
        m_verify = button("Verificar estado", 147.f, "GJ_button_01.png", menu_selector(ModerationPanel::onVerify));
        button("Conectar / Confirmar", 109.f, "GJ_button_02.png", menu_selector(ModerationPanel::onConnect));
        button("Centro de moderacion", 71.f, "GJ_button_02.png", menu_selector(ModerationPanel::onOpen));
        button("Cerrar sesion local", 33.f, "GJ_button_06.png", menu_selector(ModerationPanel::onDisconnect));
        return true;
    }

    void onVerify(CCObject*) {
        if (m_checking) return;
        m_checking = true;
        m_verify->setEnabled(false);
        m_status->setString("Consultando servidor...");
        WeakRef<ModerationPanel> self = this;
        HttpClient::get().checkModeratorAccount(GameManager::get()->m_playerName,
            GJAccountManager::get()->m_accountID, [self](bool isMod, bool isAdmin) {
                auto panel = self.lock();
                if (!panel) return;
                panel->m_checking = false;
                panel->m_verify->setEnabled(true);
                panel->m_status->setString(isAdmin ? "Administrador verificado" :
                    isMod ? "Moderador verificado" : "Sin acceso verificado. Conecta o reintenta.");
                panel->m_status->limitLabelWidth(350.f, 0.45f, 0.25f);
                panel->m_status->setColor(isMod || isAdmin ? ccc3(110, 240, 150) : ccc3(255, 195, 110));
            });
    }

    void onConnect(CCObject*) { startOrComplete(); }

    void onOpen(CCObject*) {
        if (!isVerified()) { onVerify(nullptr); return; }
        auto scene = VerificationCenterLayer::scene();
        if (!scene) return;
        onClose(nullptr);
        TransitionManager::get().pushScene(scene);
    }

    void onDisconnect(CCObject*) {
        ++s_requestGeneration;
        s_requestStarted = {};
        clearChallenge();
        HttpClient::get().setModCode("");
        Mod::get()->setSavedValue<int64_t>(kCredentialExpiresAt, 0);
        Mod::get()->setSavedValue<int64_t>("mod-credential-account", 0);
        Mod::get()->setSavedValue("mod-credential-user", std::string());
        m_status->setString("Sesion cerrada en este dispositivo");
        m_status->limitLabelWidth(350.f, 0.45f, 0.25f);
    }

public:
    static ModerationPanel* create() {
        auto ret = new ModerationPanel();
        if (ret->init()) { ret->autorelease(); return ret; }
        delete ret;
        return nullptr;
    }
};

}

void startOrComplete() {
    if (!paimon::modules::isEnabled("paimbnails.modauth.system")) {
        PaimonNotify::create("Secure Mod Code esta desactivado en Modulos.", NotificationIcon::Warning)->show();
        return;
    }
    if (requestInFlight()) {
        PaimonNotify::create("La verificacion ya esta en curso.", NotificationIcon::Info)->show();
        return;
    }

    auto* gameManager = GameManager::get();
    auto* accountManager = GJAccountManager::get();
    std::string username = gameManager ? gameManager->m_playerName : "";
    int accountID = accountManager ? accountManager->m_accountID : 0;
    if (username.empty() || accountID <= 0) {
        PaimonNotify::create("Necesitas iniciar sesion en Geometry Dash.", NotificationIcon::Error)->show();
        return;
    }

    auto* mod = Mod::get();
    auto token = mod->getSavedValue<std::string>(kChallengeToken, "");
    auto challengeUser = mod->getSavedValue<std::string>(kChallengeUser, "");
    auto challengeAccount = mod->getSavedValue<int64_t>(kChallengeAccount, 0);
    if (!token.empty() && challengeUser == username && challengeAccount == accountID) {
        complete(token, username, accountID);
        return;
    }

    if (!token.empty()) clearChallenge();
    begin(username, accountID);
}

void clearVerifiedSession() {
    s_verifiedAccount = 0;
    s_verifiedUser.clear();
    s_verifiedCredential.clear();
    s_verifiedMod = false;
    s_verifiedAdmin = false;
    Mod::get()->setSavedValue("is-verified-moderator", false);
    Mod::get()->setSavedValue("is-verified-admin", false);
}

void setVerifiedSession(std::string const& username, int accountID, bool moderator, bool admin) {
    clearVerifiedSession();
    if (!matchesAccount(username, accountID)) return;
    s_verifiedUser = username;
    s_verifiedAccount = accountID;
    s_verifiedCredential = HttpClient::get().getModCode();
    s_verifiedMod = moderator || admin;
    s_verifiedAdmin = admin;
    s_verifiedAt = std::chrono::steady_clock::now();
}

bool isVerified(bool admin) {
    return (admin ? s_verifiedAdmin : s_verifiedMod) && matchesAccount(s_verifiedUser, s_verifiedAccount) &&
        !s_verifiedCredential.empty() && s_verifiedCredential == HttpClient::get().getModCode() &&
        std::chrono::steady_clock::now() - s_verifiedAt < std::chrono::minutes(2);
}

void showPanel() {
    if (!paimon::modules::isEnabled("paimbnails.modauth.system")) {
        PaimonNotify::create("Activa la autenticacion de moderacion en Modulos.", NotificationIcon::Warning)->show();
        return;
    }
    if (auto panel = ModerationPanel::create()) panel->show();
}

}
