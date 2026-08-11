#pragma once

#include <string>
#include <vector>
#include <optional>
#include <ctime>

// Volatile chat memory for repeats and contextual follow-ups.

namespace paimon::guide {

struct ConversationTurn {
    std::string userQuery;       // Original text.
    std::string normalizedQuery; // Normalized text.
    std::string matchedIntentId; // Empty for fallback.
    std::string topicId;         // Follow-up topic.
    bool wasFunctional = false;  // Intent kind at match time.
    double matchScore = 0.0;     // Fuzzy score, 0..100.
    std::time_t timestamp = 0;
};

class ConversationMemory {
public:
// Maximum stored turns; oldest are discarded.
    static constexpr std::size_t kMaxTurns = 12;

// Recent-turn window for repeats/follow-ups.
    static constexpr std::time_t kRecentSecs = 60;

// Record a turn.
    void recordTurn(ConversationTurn turn);

// Clear memory.
    void clear();

// Number of stored turns.
    std::size_t size() const { return m_history.size(); }

// Full history, oldest first.
    std::vector<ConversationTurn> const& history() const { return m_history; }

// Last functional turn for short follow-ups.
    std::optional<ConversationTurn> lastFunctionalTurn() const;

// Recent effective topic, or empty.
    std::string lastTopicId(std::time_t withinSecs = kRecentSecs) const;

// Matches for this intent in the recent window.
    int recentMatchesOf(std::string const& intentId,
                        std::time_t withinSecs = kRecentSecs) const;

// Whether the intent was answered recently.
    bool hasJustAnswered(std::string const& intentId,
                         std::time_t withinSecs = kRecentSecs) const;

// Heuristic for a short follow-up query.
    static bool looksLikeFollowUp(std::string const& normalized);

private:
    std::vector<ConversationTurn> m_history;
};

}
