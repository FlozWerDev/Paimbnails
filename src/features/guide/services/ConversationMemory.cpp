#include "ConversationMemory.hpp"

#include <algorithm>

namespace paimon::guide {

void ConversationMemory::recordTurn(ConversationTurn turn) {
    if (turn.timestamp == 0) {
        turn.timestamp = std::time(nullptr);
    }
    m_history.push_back(std::move(turn));
    if (m_history.size() > kMaxTurns) {
        m_history.erase(m_history.begin(),
                        m_history.begin() + (m_history.size() - kMaxTurns));
    }
}

void ConversationMemory::clear() {
    m_history.clear();
}

std::optional<ConversationTurn> ConversationMemory::lastFunctionalTurn() const {
    for (auto it = m_history.rbegin(); it != m_history.rend(); ++it) {
        if (it->wasFunctional && !it->matchedIntentId.empty()) {
            return *it;
        }
    }
    return std::nullopt;
}

int ConversationMemory::recentMatchesOf(std::string const& intentId,
                                        std::time_t withinSecs) const {
    if (intentId.empty()) return 0;
    auto now = std::time(nullptr);
    int count = 0;
    for (auto const& turn : m_history) {
        if (turn.matchedIntentId == intentId
            && (now - turn.timestamp) <= withinSecs) {
            ++count;
        }
    }
    return count;
}

bool ConversationMemory::hasJustAnswered(std::string const& intentId,
                                         std::time_t withinSecs) const {
    return recentMatchesOf(intentId, withinSecs) > 0;
}

bool ConversationMemory::looksLikeFollowUp(std::string const& normalized) {
    if (normalized.empty()) return false;

    // Contar palabras (separadas por espacio en la version normalizada).
    int wordCount = 0;
    bool inWord = false;
    for (char c : normalized) {
        if (c == ' ') {
            if (inWord) { ++wordCount; inWord = false; }
        } else {
            inWord = true;
        }
    }
    if (inWord) ++wordCount;

    // Si tiene 1 o 2 palabras, comprobar si son palabras-puente tipicas de
    // continuaciones. Asi una pregunta corta como "fondos" no se trata como
    // follow-up (se matchea normal), pero "y?" o "como?" si.
    if (wordCount > 2) return false;

    // Un set pequeno y fijo de "follow-up tokens" comunes en ES y EN.
    static char const* const kFollowUpTokens[] = {
        // espanol
        "y", "como", "donde", "cuando", "porque", "y como", "y donde",
        "y eso", "y ahora", "mas", "otra vez", "explicame",
        // english
        "how", "where", "when", "why", "more", "again", "explain",
        "and", "and how", "and where",
    };

    for (auto const* tok : kFollowUpTokens) {
        if (normalized == tok) return true;
        // tambien aceptar con signos (los signos ya fueron strippeados por
        // normalize() asi que comparamos con la version sin signos).
    }
    return false;
}

} // namespace paimon::guide
