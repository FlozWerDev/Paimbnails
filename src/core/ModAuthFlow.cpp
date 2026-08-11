#include "ModAuthFlow.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/OverlayManager.hpp>

#include "RuntimeLifecycle.hpp"
#include "modules/ModuleRegistry.hpp"
#include "../utils/HttpClient.hpp"
#include "../utils/MainThreadDelay.hpp"
#include "../utils/PaimonLoadingOverlay.hpp"
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

struct AutoConfirmState {
    Ref<PaimonLoadingOverlay> overlay;
    int attempts = 0;
    int maxAttempts = 0;
};

// Kept alive while the auto-confirm polling loop is running so the raw pointer
// passed into the async callbacks never dangles.
std::shared_ptr<AutoConfirmState> s_autoState;

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
            "vuelve a pulsar <cg>Secure Mod Code</c>: detectare el comentario "
            "automaticamente con el Paimon loading y lo confirmare.",
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
            if (paimon::isRuntimeShuttingDown()) return;

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

void complete(std::string const& challengeToken, std::shared_ptr<AutoConfirmState> const& autoState) {
    auto generation = beginRequest();
    if (autoState) {
        autoState->attempts++;
        autoState->overlay->updateText(fmt::format(
            "Esperando el comentario... (intento {})", autoState->attempts
        ));
    }

    HttpClient::get().completeModCodeSetup(challengeToken, [generation, autoState, challengeToken](bool ok, std::string const& response) {
        queueInMainThread([generation, ok, response, autoState, challengeToken] {
            if (!finishRequest(generation)) return;
            if (paimon::isRuntimeShuttingDown()) return;

            auto parsed = parseResponse(response);
            if (!ok || !parsed) {
                if (autoState) {
                    // PROFILE_CODE_NOT_FOUND / retryable: keep polling until the
                    // comment shows up on the GD profile, or the challenge expires.
                    auto code = stringField(*parsed, "code");
                    // Keep polling while the user might still be posting the
                    // comment. A stale challenge, though, is a dead end — the
                    // user must generate a new one.
                    bool retryable = code == "PROFILE_CODE_NOT_FOUND"
                        || code == "GD_COMMENTS_UNAVAILABLE"
                        || code == "MOD_AUTH_RATE_LIMITED";
                    if (retryable && autoState->overlay->getParent()) {
                        if (autoState->attempts >= autoState->maxAttempts) {
                            s_autoState.reset();
                            autoState->overlay->dismiss();
                            showRequestError(response, true);
                            return;
                        }
                        // Space out retries ~3s so we don't hammer the comments
                        // endpoint / get rate limited while the user posts the code.
                        paimon::scheduleMainThreadDelay(3.f, [challengeToken, autoState]() {
                            if (paimon::isRuntimeShuttingDown()) return;
                            if (!autoState->overlay->getParent()) return;
                            complete(challengeToken, autoState);
                        });
                        return;
                    }
                    s_autoState.reset();
                    autoState->overlay->dismiss();
                    showRequestError(response, true);
                    return;
                }
                showRequestError(response, true);
                return;
            }

            auto credential = stringField(*parsed, "modCode");
            if (credential.size() != 50 || !credential.starts_with("pmc_v2_")) {
                if (autoState) {
                    s_autoState.reset();
                    autoState->overlay->dismiss();
                }
                PaimonNotify::create("El servidor devolvio una credencial invalida.", NotificationIcon::Error)->show();
                return;
            }

            HttpClient::get().setModCode(credential);
            auto* mod = Mod::get();
            mod->setSavedValue<bool>("is-verified-moderator", true);
            mod->setSavedValue<bool>("is-verified-admin", (*parsed)["isAdmin"].asBool().unwrapOr(false));
            mod->setSavedValue<int64_t>(
                kCredentialExpiresAt,
                static_cast<int64_t>((*parsed)["expiresAt"].asDouble().unwrapOr(0.0))
            );
            clearChallenge();
            if (autoState) {
                s_autoState.reset();
                autoState->overlay->dismiss();
            }
            PaimonNotify::create("Mod Code seguro activado y sincronizado.", NotificationIcon::Success)->show();
        });
    });
}

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
        // Challenge pending: show the Paimon loading overlay and poll
        // /api/mod-auth/complete until the profile comment shows up. The user
        // just needs to post PAI-MOD-XXXX and wait — no need to tap again.
        //
        // Parent the overlay to geode::OverlayManager — the same top-most host
        // the custom cursor uses (z INT_MAX) — with a z-order just below it, so
        // the loading screen renders above every scene, popup and transition
        // while never covering the custom cursor.
        auto* overlay = PaimonLoadingOverlay::create("Esperando el comentario...", 40.f);
        overlay->setID("paimon-modauth-overlay"_spr);
        constexpr int kModAuthOverlayZOrder = 999500;
        if (auto* host = geode::OverlayManager::get()) {
            overlay->show(host, kModAuthOverlayZOrder);
        } else if (auto* scene = CCDirector::get()->getRunningScene()) {
            overlay->show(scene, 300);
        }

        auto state = std::make_shared<AutoConfirmState>();
        state->overlay = overlay;
        // Challenge TTL is 15 min; poll ~45 times with the 4s cadence before
        // giving up (each complete() round-trip also takes ~1s).
        state->maxAttempts = 45;
        s_autoState = state;
        complete(token, state);
        return;
    }

    if (!token.empty()) clearChallenge();
    begin(username, accountID);
}

}
