#pragma once

#include <string>
#include <vector>
#include <optional>
#include <ctime>

// Short-term memory for the Paimon chat: detects repeated intents, enables
// contextual follow-ups, and avoids duplicate consecutive answers. Volatile
// (not persisted), so each session starts fresh.

namespace paimon::guide {

struct ConversationTurn {
    std::string userQuery;       // as the user typed it
    std::string normalizedQuery; // after normalize() (no accents, lowercase)
    std::string matchedIntentId; // empty if fallback
    bool wasFunctional = false;  // intent kind at match time
    double matchScore = 0.0;     // fuzzy score (0-100)
    std::time_t timestamp = 0;
};

class ConversationMemory {
public:
    // Max stored turns (oldest discarded).
    static constexpr std::size_t kMaxTurns = 12;

    // Window (seconds) for treating a turn as "recent" for repeats/follow-ups.
    static constexpr std::time_t kRecentSecs = 60;

    // Record a new turn.
    void recordTurn(ConversationTurn turn);

    // Clear all memory (e.g. when closing the chat).
    void clear();

    // Number of turns in history.
    std::size_t size() const { return m_history.size(); }

    // Full history (oldest first).
    std::vector<ConversationTurn> const& history() const { return m_history; }

    // Last turn that matched a Functional intent, for resolving short follow-ups.
    std::optional<ConversationTurn> lastFunctionalTurn() const;

    // How many times this intent matched in the last N seconds.
    int recentMatchesOf(std::string const& intentId,
                        std::time_t withinSecs = kRecentSecs) const;

    // True if the intent was answered in the last N seconds.
    bool hasJustAnswered(std::string const& intentId,
                         std::time_t withinSecs = kRecentSecs) const;

    // Heuristic: 1-2 word query that sounds like a follow-up; caller may reuse lastFunctionalTurn().
    static bool looksLikeFollowUp(std::string const& normalized);

private:
    std::vector<ConversationTurn> m_history;
};

} // namespace paimon::guide
