#pragma once

#include <string>
#include <vector>
#include <optional>
#include <ctime>

// Volatile chat memory for repeats and contextual follow-ups.

namespace paimon::guide {

struct ConversationTurn {
    std::string userQuery;       // Original text.
    std::string matchedIntentId; // Empty for fallback.
    std::string topicId;         // Follow-up topic.
    bool wasFunctional = false;  // Intent kind at match time.
    std::time_t timestamp = 0;
};

class ConversationMemory {
public:
    // oldest are discarded.
    static constexpr std::size_t kMaxTurns = 12;

    // recent-turn window for repeats/follow-ups.
    static constexpr std::time_t kRecentSecs = 60;

    void recordTurn(ConversationTurn turn);
    void clear();
    std::size_t size() const { return m_history.size(); }
    std::vector<ConversationTurn> const& history() const { return m_history; }

    // last functional turn for short follow-ups.
    std::optional<ConversationTurn> lastFunctionalTurn() const;

    // recent effective topic, or empty.
    std::string lastTopicId(std::time_t withinSecs = kRecentSecs) const;

    // matches for this intent in the recent window.
    int recentMatchesOf(std::string const& intentId,
                        std::time_t withinSecs = kRecentSecs) const;

    // heuristic for a short follow-up query.
    static bool looksLikeFollowUp(std::string const& normalized);

private:
    std::vector<ConversationTurn> m_history;
};

}
