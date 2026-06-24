#pragma once

#include "GuideIntents.hpp"
#include <string>
#include <vector>
#include <unordered_map>

// Paigorit V1: Paimon's intent matcher. Uses a "best-keyword-weight wins" model:
// among intents that qualify (a keyword scores >= MATCH_FLOOR), the one whose
// matched keyword has the highest weight wins. The fuzzy score only decides
// whether an intent qualifies, not which qualified intent wins. Suggested
// weights: 50 generic, 80 feature-specific, 100 identity, 150 unique.

namespace paimon::guide {

struct ScoredIntent {
    GuideIntent const* intent = nullptr;

    // Best fuzzy score across the intent's keywords vs the query.
    double bestKeywordFuzzy = 0.0;

    // True if a multi-word keyword appears as a contiguous run in the query.
    bool hasCompoundMatch = false;

    // True if a keyword exactly equals a query token (post-stopwords).
    bool hasExactTokenMatch = false;

    // Match confidence (0-30): combines the flags and fuzzy score.
    double confidenceBonus = 0.0;

    // Final score: intent weight + confidenceBonus.
    double finalScore = 0.0;

    // True if the intent passed MATCH_FLOOR; unqualified intents are excluded from the ranking.
    bool qualified = false;
};

struct PaigoritResult {
    GuideIntent const* best = nullptr;
    double bestScore = 0.0;
    double bestRawFuzzy = 0.0;
    bool ambiguous = false;
    std::vector<ScoredIntent> ranking; // filtered to qualified, sorted
};

class PaigoritV1 {
public:
    // Minimum fuzzy score for a keyword to count as matched; below it, the intent is disqualified.
    static constexpr double kMatchFloor = 70.0;

    // Higher floor for conversational intents to avoid false positives on technical queries.
    static constexpr double kMatchFloorConversational = 85.0;

    // For >=4-word queries, conversational intents require a near-perfect match.
    static constexpr double kMatchFloorConversationalLong = 92.0;

    // If the top two finalScores differ by less than this, mark as ambiguous.
    static constexpr double kAmbiguityGap = 5.0;

    static PaigoritResult run(std::vector<GuideIntent> const& intents,
                              std::string const& normalizedQuery,
                              std::vector<std::string> const& queryTokens,
                              std::string const& langId);

private:
    // Max fuzzy score (0-100) between the query's expanded forms and a keyword.
    static double bestFuzzyAgainstKeyword(std::string const& normalizedQuery,
                                          std::vector<std::string> const& expandedTokens,
                                          std::string const& keyword);

    // True if a multi-word keyword appears as a contiguous run of tokens (after synonym/stem).
    static bool keywordAppearsAsCompound(std::vector<std::vector<std::string>> const& tokenForms,
                                         std::vector<std::string> const& kwTokens);

    // True if any token form exactly matches the keyword.
    static bool anyTokenFormEquals(std::vector<std::vector<std::string>> const& tokenForms,
                                   std::string const& keyword);
};

} // namespace paimon::guide
