#pragma once

#include <algorithm>
#include <cctype>
#include <functional>
#include <memory>
#include <string>
#include <utility>

namespace paimon::twitch {

struct VerificationIdentity {
    int accountID = 0;
    std::string username;
};

enum class VerificationMode { Manual, Automatic };
enum class VerificationStage { Idle, Preparing, Publishing, Waiting, Checking, Cleaning, Finished };

struct VerificationChallenge {
    bool ok = false;
    VerificationIdentity identity;
    std::string code;
    std::string error;
};

struct VerificationCheck {
    bool ok = false;
    bool pending = false;
    VerificationIdentity identity;
    std::string token;
    std::string error;
};

struct VerificationActions {
    using UploadCallback = std::function<void(bool, int, std::string)>;
    using CheckCallback = std::function<void(VerificationCheck)>;
    std::function<bool()> isCurrentAccount;
    std::function<void(std::function<void(VerificationChallenge)>)> requestCode;
    std::function<void(std::string const&, UploadCallback)> publishComment;
    std::function<void(CheckCallback)> checkComment;
    std::function<void(int, std::function<void(bool, std::string)>)> deleteComment;
    std::function<void(std::string const&)> storeToken;
    std::function<void(int, std::string const&)> rememberComment;
    std::function<void()> reconnect;
    std::function<void(std::function<void()>)> schedule;
};

// all actions and callbacks run on the main thread. No GD credentials enter this state machine.
class RequestVerificationFlow : public std::enable_shared_from_this<RequestVerificationFlow> {
public:
    RequestVerificationFlow(VerificationIdentity identity, VerificationActions actions, int pendingComment = 0)
      : m_identity(std::move(identity)), m_actions(std::move(actions)), m_commentID(pendingComment) {
        if (pendingComment > 0) m_status = "Quedo un comentario temporal. Puedes reintentar su borrado.";
    }

    VerificationStage stage() const { return m_stage; }
    std::string const& status() const { return m_status; }
    std::string const& code() const { return m_code; }
    bool verified() const { return m_verified; }
    int commentID() const { return m_commentID; }
    bool busy() const {
        return m_networkPending || m_stage == VerificationStage::Preparing || m_stage == VerificationStage::Publishing
            || m_stage == VerificationStage::Checking || m_stage == VerificationStage::Cleaning
            || (m_mode == VerificationMode::Automatic && m_stage == VerificationStage::Waiting);
    }

    static bool validCode(std::string const& code) {
        return code.size() == 10 && code.starts_with("PAI-")
            && std::all_of(code.begin() + 4, code.end(), [](unsigned char ch) {
                return (ch >= 'A' && ch <= 'Z') || (ch >= '2' && ch <= '9');
            });
    }

    static bool sameIdentity(VerificationIdentity const& lhs, VerificationIdentity const& rhs) {
        if (lhs.accountID <= 0 || lhs.accountID != rhs.accountID || lhs.username.size() != rhs.username.size()) return false;
        return std::equal(lhs.username.begin(), lhs.username.end(), rhs.username.begin(),
            [](unsigned char a, unsigned char b) { return std::tolower(a) == std::tolower(b); });
    }

    void start(VerificationMode mode) {
        if (busy() || m_commentID > 0) return;
        if (!m_actions.isCurrentAccount()) { fail("La cuenta de GD cambio. Abre otra vez la verificacion."); return; }
        m_mode = mode;
        m_cancelled = false;
        m_verified = false;
        m_attempts = 0;
        ++m_generation;
        m_error.clear();
        m_code.clear();
        m_stage = VerificationStage::Preparing;
        m_status = "Generando codigo para tu cuenta...";
        m_networkPending = true;
        m_actions.requestCode([self = shared_from_this()](VerificationChallenge challenge) {
            self->m_networkPending = false;
            if (self->m_cancelled) { self->finish(); return; }
            if (!self->m_actions.isCurrentAccount()) { self->fail("La cuenta de GD cambio."); return; }
            if (!challenge.ok || !sameIdentity(self->m_identity, challenge.identity) || !validCode(challenge.code)) {
                self->fail(challenge.error.empty() ? "El servidor no devolvio un codigo valido para esta cuenta." : challenge.error);
                return;
            }
            self->m_code = std::move(challenge.code);
            if (self->m_mode == VerificationMode::Manual) {
                self->m_stage = VerificationStage::Waiting;
                self->m_status = "Copia el codigo, publicalo en tu perfil de GD y pulsa Comprobar.";
                return;
            }
            self->m_stage = VerificationStage::Publishing;
            self->m_status = "Publicando el comentario temporal en tu perfil de GD...";
            self->m_networkPending = true;
            self->m_actions.publishComment(self->m_code, [self](bool ok, int id, std::string error) {
                self->m_networkPending = false;
                if (!ok || id <= 0) { self->fail(error.empty() ? "GD no confirmo la publicacion. Revisa tu perfil antes de reintentar." : error); return; }
                self->m_commentID = id;
                self->m_actions.rememberComment(id, self->m_code);
                if (self->m_cancelled) { self->retryCleanup(); return; }
                self->m_stage = VerificationStage::Waiting;
                self->check();
            });
        });
    }

    void check() {
        if (m_stage != VerificationStage::Waiting || m_cancelled) return;
        if (!m_actions.isCurrentAccount()) { fail("La cuenta de GD cambio. No se guardo otra sesion."); return; }
        m_stage = VerificationStage::Checking;
        m_status = "El servidor esta comprobando el comentario publico...";
        ++m_attempts;
        m_networkPending = true;
        m_actions.checkComment([self = shared_from_this()](VerificationCheck result) {
            self->m_networkPending = false;
            if (self->m_cancelled) { self->retryCleanup(); return; }
            if (!self->m_actions.isCurrentAccount()) { self->fail("La cuenta de GD cambio. No se guardo otra sesion."); return; }
            if (result.ok) {
                if (!sameIdentity(self->m_identity, result.identity) || result.token.empty()) {
                    self->fail("La respuesta de verificacion no corresponde a tu cuenta."); return;
                }
                self->m_actions.storeToken(result.token);
                self->m_verified = true;
                self->m_actions.reconnect();
                if (self->m_commentID > 0) self->retryCleanup();
                else self->finish();
                return;
            }
            if (result.pending) {
                self->m_stage = VerificationStage::Waiting;
                self->m_status = "El comentario aun no aparece. Esperando a los servidores de GD...";
                if (self->m_mode == VerificationMode::Manual) return;
                if (self->m_attempts < 24) {
                    self->m_actions.schedule([self, generation = self->m_generation] {
                        if (!self->m_cancelled && self->m_generation == generation) self->check();
                    });
                    return;
                }
            }
            self->fail(result.error.empty() ? "No se pudo verificar. Puedes probar el modo manual." : result.error);
        });
    }

    void retryCleanup() {
        if (m_cleanupBusy) return;
        if (m_commentID <= 0) { finish(); return; }
        if (!m_actions.isCurrentAccount()) {
            m_stage = VerificationStage::Finished;
            m_status = "Vuelve a la cuenta original para borrar su comentario temporal.";
            return;
        }
        m_cleanupBusy = true;
        m_stage = VerificationStage::Cleaning;
        m_status = "Borrando solo el comentario temporal de esta verificacion...";
        m_actions.deleteComment(m_commentID, [self = shared_from_this()](bool ok, std::string error) {
            self->m_cleanupBusy = false;
            if (ok) {
                self->m_commentID = 0;
                self->m_actions.rememberComment(0, {});
                self->finish();
            } else {
                self->m_stage = VerificationStage::Finished;
                self->m_status = (self->m_verified ? "Cuenta verificada. " : "")
                    + (error.empty() ? std::string("No se pudo borrar el comentario; reintenta o borralo desde tu perfil.") : error);
            }
        });
    }

    void cancel() {
        m_cancelled = true;
        // an upload already in flight still needs its callback to learn the exact new comment ID.
        if (m_stage == VerificationStage::Publishing || m_cleanupBusy) return;
        if (m_commentID > 0) retryCleanup();
        else finish();
    }

    void acknowledgeManualCleanup() {
        if (busy()) return;
        m_commentID = 0;
        m_actions.rememberComment(0, {});
        finish();
    }

private:
    void fail(std::string error) {
        m_error = std::move(error);
        if (m_commentID > 0 && m_mode == VerificationMode::Automatic) retryCleanup();
        else finish();
    }
    void finish() {
        m_stage = VerificationStage::Finished;
        m_status = m_verified ? (m_mode == VerificationMode::Manual
                ? "Cuenta verificada. Ya puedes borrar el codigo de tu perfil; la pagina se reconecta."
                : "Cuenta verificada. Tu pagina se reconecta automaticamente.")
            : m_cancelled ? "Verificacion cancelada."
            : m_error.empty() ? "Comentario temporal eliminado." : m_error;
    }

    VerificationIdentity m_identity;
    VerificationActions m_actions;
    VerificationMode m_mode = VerificationMode::Manual;
    VerificationStage m_stage = VerificationStage::Idle;
    std::string m_status = "Elige verificacion automatica o manual.";
    std::string m_code;
    std::string m_error;
    int m_commentID = 0;
    int m_attempts = 0;
    unsigned m_generation = 0;
    bool m_networkPending = false;
    bool m_verified = false;
    bool m_cancelled = false;
    bool m_cleanupBusy = false;
};

} // namespace paimon::twitch
