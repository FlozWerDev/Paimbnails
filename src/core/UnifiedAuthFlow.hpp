#pragma once

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace paimon::auth {

struct AuthIdentity {
    int accountID = 0;
    std::string username;
};

struct SessionRoles {
    bool authenticated = false;
    bool isAdmin = false;
    bool isModerator = false;
    bool isVip = false;
    bool isHelper = false;
    bool isIdea = false;
    std::string primaryRole = "user";
    std::vector<std::string> permissions;

    bool hasModAccess() const { return authenticated && (isModerator || isAdmin); }
    bool hasPermission(std::string_view permission) const {
        if (!authenticated) return false;
        return std::find(permissions.begin(), permissions.end(), permission) != permissions.end();
    }
};

enum class LoginMode { Manual, Automatic };
enum class LoginStage { Idle, Preparing, Publishing, Waiting, Completing, Cleaning, Finished };

struct LoginChallenge {
    bool ok = false;
    AuthIdentity identity;
    std::string challengeToken;
    std::string profileCode;
    std::string error;
};

struct LoginCompletion {
    bool ok = false;
    bool pending = false;
    AuthIdentity identity;
    std::string sessionToken;
    int64_t expiresAt = 0;
    SessionRoles roles;
    std::string error;
};

struct LoginActions {
    using ChallengeCallback = std::function<void(LoginChallenge)>;
    using PublishCallback = std::function<void(bool ok, int commentID, std::string error)>;
    using CompleteCallback = std::function<void(LoginCompletion)>;
    using CleanupCallback = std::function<void(bool ok, std::string error)>;

    std::function<bool()> isCurrentAccount;
    std::function<void(ChallengeCallback)> startChallenge;
    std::function<void(std::string const& code, PublishCallback)> publishComment;
    std::function<void(std::string const& challengeToken, CompleteCallback)> completeChallenge;
    std::function<void(int commentID, CleanupCallback)> deleteComment;
    std::function<void(std::string const& token, AuthIdentity const& identity, int64_t expiresAt, SessionRoles const& roles)> storeSession;
    std::function<void(int commentID)> rememberComment;
    std::function<void()> onLoggedIn;
    std::function<void(std::function<void()>)> schedule;
};

// Pure state machine for unified Paimbnails login (Automatic 1-click + Manual).
// No GD credentials (GJP2) enter this state machine; all callbacks run on the main thread.
class UnifiedAuthFlow : public std::enable_shared_from_this<UnifiedAuthFlow> {
public:
    UnifiedAuthFlow(AuthIdentity identity, LoginActions actions, int pendingComment = 0)
        : m_identity(std::move(identity)), m_actions(std::move(actions)), m_commentID(pendingComment) {
        if (pendingComment > 0) {
            m_status = "Quedo un comentario temporal. Puedes reintentar su borrado.";
        }
    }

    LoginStage stage() const { return m_stage; }
    std::string const& status() const { return m_status; }
    std::string const& code() const { return m_code; }
    std::string const& challengeToken() const { return m_challengeToken; }
    SessionRoles const& roles() const { return m_roles; }
    bool loggedIn() const { return m_loggedIn; }
    int commentID() const { return m_commentID; }

    bool busy() const {
        return m_networkPending
            || m_stage == LoginStage::Preparing
            || m_stage == LoginStage::Publishing
            || m_stage == LoginStage::Completing
            || m_stage == LoginStage::Cleaning
            || (m_mode == LoginMode::Automatic && m_stage == LoginStage::Waiting);
    }

    // Strict check: only allow server-generated profile challenge codes to be posted.
    // Prevents an untrusted/MITM response from making automatic mode post arbitrary text.
    static bool validProfileCode(std::string_view code) {
        if (code.size() != 19 || !code.starts_with("PAI-AUTH-")) return false;
        static constexpr std::string_view kAlphabet = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
        for (size_t i = 9; i < code.size(); ++i) {
            if (kAlphabet.find(code[i]) == std::string_view::npos) return false;
        }
        return true;
    }

    // Strict check: unified v3 session tokens bind accountID and carry 43-char base64url secret.
    static bool validSessionToken(std::string_view token, int expectedAccountID) {
        if (expectedAccountID <= 0 || !token.starts_with("psess_v3_")) return false;
        auto rest = token.substr(9);
        auto sep = rest.find('_');
        if (sep == std::string_view::npos || sep == 0 || sep > 15) return false;
        int parsedID = 0;
        auto idPart = rest.substr(0, sep);
        auto [ptr, ec] = std::from_chars(idPart.data(), idPart.data() + idPart.size(), parsedID);
        if (ec != std::errc{} || ptr != idPart.data() + idPart.size() || parsedID != expectedAccountID) {
            return false;
        }
        auto secretPart = rest.substr(sep + 1);
        if (secretPart.size() != 43) return false;
        return std::all_of(secretPart.begin(), secretPart.end(), [](unsigned char ch) {
            return std::isalnum(ch) || ch == '-' || ch == '_';
        });
    }

    static bool sameIdentity(AuthIdentity const& lhs, AuthIdentity const& rhs) {
        if (lhs.accountID <= 0 || lhs.accountID != rhs.accountID || lhs.username.empty() ||
            lhs.username.size() != rhs.username.size()) {
            return false;
        }
        return std::equal(lhs.username.begin(), lhs.username.end(), rhs.username.begin(),
            [](unsigned char a, unsigned char b) { return std::tolower(a) == std::tolower(b); });
    }

    void start(LoginMode mode) {
        if (busy() || m_commentID > 0) return;
        if (!m_actions.isCurrentAccount || !m_actions.isCurrentAccount()) {
            fail("La cuenta de GD cambio. Abre otra vez el panel de sesion.");
            return;
        }
        m_mode = mode;
        m_cancelled = false;
        m_loggedIn = false;
        m_attempts = 0;
        ++m_generation;
        m_error.clear();
        m_code.clear();
        m_challengeToken.clear();
        m_stage = LoginStage::Preparing;
        m_status = "Generando desafio seguro para tu cuenta...";
        m_networkPending = true;

        auto const generation = m_generation;
        m_actions.startChallenge([self = shared_from_this(), generation](LoginChallenge challenge) {
            self->m_networkPending = false;
            if (self->m_cancelled || self->m_generation != generation) {
                self->finish();
                return;
            }
            if (!self->m_actions.isCurrentAccount || !self->m_actions.isCurrentAccount()) {
                self->fail("La cuenta de GD cambio.");
                return;
            }
            if (!challenge.ok || !sameIdentity(self->m_identity, challenge.identity) ||
                challenge.challengeToken.empty() || challenge.challengeToken.size() > 2048 ||
                !validProfileCode(challenge.profileCode)) {
                self->fail(challenge.error.empty()
                    ? "El servidor no devolvio un desafio valido para tu cuenta."
                    : challenge.error);
                return;
            }
            self->m_challengeToken = std::move(challenge.challengeToken);
            self->m_code = std::move(challenge.profileCode);
            if (self->m_mode == LoginMode::Manual) {
                self->m_stage = LoginStage::Waiting;
                self->m_status = "Copia el codigo, publicalo en tu perfil de GD y pulsa Confirmar.";
                return;
            }
            self->m_stage = LoginStage::Publishing;
            self->m_status = "Publicando comentario temporal en tu perfil de GD...";
            self->m_networkPending = true;
            self->m_actions.publishComment(self->m_code, [self, generation](bool ok, int id, std::string error) {
                self->m_networkPending = false;
                if (!ok || id <= 0) {
                    self->fail(error.empty()
                        ? "GD no confirmo la publicacion. Revisa tu perfil o usa el modo manual."
                        : error);
                    return;
                }
                self->m_commentID = id;
                if (self->m_actions.rememberComment) self->m_actions.rememberComment(id);
                if (self->m_cancelled || self->m_generation != generation) {
                    self->retryCleanup();
                    return;
                }
                self->m_stage = LoginStage::Waiting;
                self->confirm();
            });
        });
    }

    void confirm() {
        if (m_stage != LoginStage::Waiting || m_cancelled || m_challengeToken.empty()) return;
        if (!m_actions.isCurrentAccount || !m_actions.isCurrentAccount()) {
            fail("La cuenta de GD cambio. No se guardo otra sesion.");
            return;
        }
        m_stage = LoginStage::Completing;
        m_status = "Verificando tu comentario y permisos en el servidor...";
        ++m_attempts;
        m_networkPending = true;

        auto const generation = m_generation;
        m_actions.completeChallenge(m_challengeToken, [self = shared_from_this(), generation](LoginCompletion result) {
            self->m_networkPending = false;
            if (self->m_cancelled || self->m_generation != generation) {
                if (self->m_commentID > 0) self->retryCleanup();
                else self->finish();
                return;
            }
            if (!self->m_actions.isCurrentAccount || !self->m_actions.isCurrentAccount()) {
                self->fail("La cuenta de GD cambio. No se guardo otra sesion.");
                return;
            }
            if (result.ok) {
                if (!sameIdentity(self->m_identity, result.identity) ||
                    !validSessionToken(result.sessionToken, self->m_identity.accountID)) {
                    self->fail("La respuesta de sesion no corresponde a tu cuenta.");
                    return;
                }
                result.roles.authenticated = true;
                self->m_roles = result.roles;
                self->m_challengeToken.clear();
                if (self->m_actions.storeSession) {
                    self->m_actions.storeSession(result.sessionToken, result.identity, result.expiresAt, result.roles);
                }
                self->m_loggedIn = true;
                if (self->m_actions.onLoggedIn) self->m_actions.onLoggedIn();
                if (self->m_commentID > 0) self->retryCleanup();
                else self->finish();
                return;
            }
            if (result.pending) {
                self->m_stage = LoginStage::Waiting;
                self->m_status = "El comentario aun no aparece en tu perfil de GD...";
                if (self->m_mode == LoginMode::Manual) return;
                if (self->m_attempts < 24 && self->m_actions.schedule) {
                    self->m_actions.schedule([self, generation] {
                        if (!self->m_cancelled && self->m_generation == generation) self->confirm();
                    });
                    return;
                }
            }
            self->fail(result.error.empty()
                ? "No se pudo completar el inicio de sesion. Prueba el modo manual."
                : result.error);
        });
    }

    void retryCleanup() {
        if (m_cleanupBusy) return;
        if (m_commentID <= 0) {
            finish();
            return;
        }
        if (!m_actions.isCurrentAccount || !m_actions.isCurrentAccount()) {
            m_stage = LoginStage::Finished;
            m_status = "Vuelve a la cuenta original para borrar su comentario temporal.";
            return;
        }
        m_cleanupBusy = true;
        m_stage = LoginStage::Cleaning;
        m_status = "Borrando el comentario temporal de verificacion...";
        m_actions.deleteComment(m_commentID, [self = shared_from_this()](bool ok, std::string error) {
            self->m_cleanupBusy = false;
            if (ok) {
                self->m_commentID = 0;
                if (self->m_actions.rememberComment) self->m_actions.rememberComment(0);
                self->finish();
            } else {
                self->m_stage = LoginStage::Finished;
                self->m_status = (self->m_loggedIn ? "Sesion iniciada. " : "")
                    + (error.empty()
                        ? std::string("No se pudo borrar el comentario; reintenta o borralo desde tu perfil.")
                        : error);
            }
        });
    }

    void cancel() {
        m_cancelled = true;
        m_challengeToken.clear();
        if (m_stage == LoginStage::Publishing || m_cleanupBusy) return;
        if (m_commentID > 0) retryCleanup();
        else finish();
    }

    void acknowledgeManualCleanup() {
        if (busy()) return;
        m_commentID = 0;
        if (m_actions.rememberComment) m_actions.rememberComment(0);
        finish();
    }

private:
    void fail(std::string error) {
        m_error = std::move(error);
        m_challengeToken.clear();
        if (m_commentID > 0 && m_mode == LoginMode::Automatic) retryCleanup();
        else finish();
    }

    void finish() {
        m_stage = LoginStage::Finished;
        if (m_loggedIn) {
            m_status = m_mode == LoginMode::Manual
                ? "Sesion iniciada con tus permisos. Ya puedes borrar el codigo de tu perfil."
                : "Sesion iniciada con tus permisos. Comentario temporal eliminado.";
        } else if (m_cancelled) {
            m_status = "Inicio de sesion cancelado.";
        } else {
            m_status = m_error.empty() ? "Comentario temporal eliminado." : m_error;
        }
    }

    AuthIdentity m_identity;
    LoginActions m_actions;
    LoginMode m_mode = LoginMode::Manual;
    LoginStage m_stage = LoginStage::Idle;
    std::string m_status = "Inicia sesion en 1 clic (Automatico) o modo Manual.";
    std::string m_code;
    std::string m_challengeToken;
    std::string m_error;
    SessionRoles m_roles;
    int m_commentID = 0;
    int m_attempts = 0;
    unsigned m_generation = 0;
    bool m_networkPending = false;
    bool m_loggedIn = false;
    bool m_cancelled = false;
    bool m_cleanupBusy = false;
};

} // namespace paimon::auth
