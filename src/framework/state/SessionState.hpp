#pragma once

// SessionState.hpp — Estado de sesion tipado para flujo de navegacion.
// Reemplaza las keys transitorias de Mod::get()->setSavedValue() con un
// singleton explicito que se reinicia al cerrar el juego.
// Las keys persistentes (config de usuario, server status) siguen en SavedValue.

#include <string>

namespace paimon {

struct VerificationContext {
    bool openFromThumbs       = false;
    bool openFromReport       = false;
    bool openFromQueue        = false;
    bool reopenQueue          = false;
    bool fromReportPopup      = false;
    int  queueLevelID         = -1;
    int  queueCategory        = -1;   // PendingCategory enum
    int  verificationCategory = -1;   // para popups
};

class SessionState {
public:
    static SessionState& get() {
        static SessionState instance;
        return instance;
    }

    // ── Navegacion general ──────────────────────────────────────────
    int         currentListID          = 0;
    std::string lastNavigationOrigin;

    // ── Contexto de verificacion/moderacion ─────────────────────────
    VerificationContext verification;

    // ── Helpers ─────────────────────────────────────────────────────

    // Consume un flag one-shot: lo lee y lo resetea en una sola llamada.
    // Uso: if (SessionState::get().consumeFlag(state.verification.openFromThumbs)) { ... }
    static bool consumeFlag(bool& flag) {
        bool was = flag;
        flag = false;
        return was;
    }

    // Consume un int one-shot: devuelve el valor y lo resetea a -1.
    static int consumeInt(int& value, int resetTo = -1) {
        int was = value;
        value = resetTo;
        return was;
    }

    // Resetea todo el estado de verificacion (e.g. al salir de flujo de moderacion)
    void resetVerification() {
        verification = VerificationContext{};
    }

    // Resetea TODO el estado transitorio
    void resetAll() {
        currentListID = 0;
        lastNavigationOrigin.clear();
        resetVerification();
    }

private:
    SessionState() = default;
    ~SessionState() = default;
    SessionState(SessionState const&) = delete;
    SessionState& operator=(SessionState const&) = delete;
};

} // namespace paimon
