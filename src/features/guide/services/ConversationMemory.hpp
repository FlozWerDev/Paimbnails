#pragma once

#include <string>
#include <vector>
#include <optional>
#include <ctime>

// ─────────────────────────────────────────────────────────────────────────────
// ConversationMemory
//
// Memoria a corto plazo para la conversacion con Paimon. Permite que el
// chat tenga estabilidad entre turnos:
//
//   1. Detecta repeticiones del mismo intent ("Como dije antes...").
//   2. Permite follow-ups contextuales ("Y como?", "more?") que reusan el
//      ultimo intent funcional.
//   3. Recuerda los ultimos N turnos para evitar respuestas duplicadas
//      seguidas.
//
// La memoria es por-instancia del servicio (singleton) y vive mientras el
// mod este cargado. Se podria persistir a disco con setSavedValue, pero
// preferimos memoria volatil para que cada sesion sea fresca.
// ─────────────────────────────────────────────────────────────────────────────

namespace paimon::guide {

struct ConversationTurn {
    std::string userQuery;       // tal cual escribio el usuario
    std::string normalizedQuery; // tras normalize() (sin acentos, lowercase)
    std::string matchedIntentId; // empty si fallback
    bool wasFunctional = false;  // intent kind al momento del match
    double matchScore = 0.0;     // score fuzzy (0-100)
    std::time_t timestamp = 0;
};

class ConversationMemory {
public:
    // Maximo de turnos guardados (los mas viejos se descartan).
    static constexpr std::size_t kMaxTurns = 12;

    // Tiempo (segundos) que un turno se considera "reciente" para detectar
    // repeticiones / follow-ups. Si pasa mas, ya no aplica.
    static constexpr std::time_t kRecentSecs = 60;

    // Registra un nuevo turno.
    void recordTurn(ConversationTurn turn);

    // Borra toda la memoria (al cerrar el chat por ejemplo).
    void clear();

    // Numero de turnos en la historia.
    std::size_t size() const { return m_history.size(); }

    // Acceso a la historia entera (mas viejo primero).
    std::vector<ConversationTurn> const& history() const { return m_history; }

    // Devuelve el ultimo turno donde matcheo un intent FUNCTIONAL (cursor,
    // fondos, ...). Util para que la guia sepa "de que estabamos hablando"
    // si el usuario hace un follow-up corto.
    std::optional<ConversationTurn> lastFunctionalTurn() const;

    // Cuantas veces (en los ultimos N segundos) matcheo este intent.
    int recentMatchesOf(std::string const& intentId,
                        std::time_t withinSecs = kRecentSecs) const;

    // True si el intent fue contestado en los ultimos N segundos.
    bool hasJustAnswered(std::string const& intentId,
                         std::time_t withinSecs = kRecentSecs) const;

    // Heuristica simple: la query tiene 1-2 palabras y suena como follow-up
    // ("y?", "como?", "more?", "y eso?"). En ese caso el caller puede
    // querer reusar el lastFunctionalTurn().
    static bool looksLikeFollowUp(std::string const& normalized);

private:
    std::vector<ConversationTurn> m_history;
};

} // namespace paimon::guide
