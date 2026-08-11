#include "ConversationMemory.hpp"

#include <algorithm>

namespace paimon::guide {

void ConversationMemory::recordTurn(ConversationTurn turn) {
    if (turn.timestamp == 0) {
        turn.timestamp = std::time(nullptr);
    }
    if (turn.topicId.empty()) {
        turn.topicId = turn.matchedIntentId;
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

std::string ConversationMemory::lastTopicId(std::time_t withinSecs) const {
    auto now = std::time(nullptr);
    for (auto it = m_history.rbegin(); it != m_history.rend(); ++it) {
        if (it->topicId.empty()) continue;
        if ((now - it->timestamp) > withinSecs) continue;
        return it->topicId;
    }
    return {};
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

    // Count words (space-separated in the normalized form).
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

    // 1-2 words: only treat as a follow-up if they're typical bridge words,
    // so a short query like "fondos" matches normally but "y?" or "como?" don't.
    if (wordCount > 2) return false;

    // Small fixed set of common follow-up tokens (ES + EN).
    static char const* const kFollowUpTokens[] = {
        "y", "como", "donde", "cuando", "porque", "y como", "y donde",
        "y eso", "y ahora", "mas", "otra vez", "explicame",
        "how", "where", "when", "why", "more", "again", "explain",
        "and", "and how", "and where",
    };

    for (auto const* tok : kFollowUpTokens) {
        if (normalized == tok) return true;
        // punctuation was already stripped by normalize(), so compare against the stripped form.
    }
    return false;
}

} // namespace paimon::guide
