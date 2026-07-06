#pragma once

#include "GuideIntents.hpp"
#include <string>
#include <vector>
#include <unordered_map>

// Paigorit V1: Paimon's intent matcher. Ranks qualified intents by a blended
// score: each intent's curated weight is scaled by how well the query actually
// matched it (quality factor from the best fuzzy score), then boosted by
// compound/exact hits and by how much of the query it covers. A weak match to a
// high-weight intent no longer beats a strong match to a slightly lower-weight
// one. The fuzzy floor still decides whether an intent qualifies at all.
// Suggested weights: 50 generic, 80 feature-specific, 100 identity, 150 unique.

namespace paimon::guide {

struct ScoredIntent {
    GuideIntent const* intent = nullptr;

    // Best fuzzy score across the intent's keywords vs the query (incl. phrase-level).
    double bestKeywordFuzzy = 0.0;

    // Best fuzzy that came from token-level evidence (exact/stem/typo/compound).
    // Phrase-level partials don't count here; used to gate qualification.
    double bestAnchoredFuzzy = 0.0;

    // True if a multi-word keyword appears as a contiguous run in the query.
    bool hasCompoundMatch = false;

    // True if a keyword exactly equals a query token (post-stopwords).
    bool hasExactTokenMatch = false;

    // True if the whole normalized query exactly equals one of the intent's
    // keywords (the user typed the exact name/alias) - the strongest signal.
    bool hasFullExactMatch = false;

    // Fraction (0-1) of the query's content tokens this intent explains.
    double coverageRatio = 0.0;

    // Match-quality tier (2 high / 1 medium / 0 weak). Primary ranking key, so a
    // strong match always outranks a weak one regardless of intent weight.
    int tier = 0;

    // Match confidence: compound/exact/high-fuzzy flags plus coverage.
    double confidenceBonus = 0.0;

    // Within-tier score: (weight * qualityFactor) + confidenceBonus.
    double finalScore = 0.0;

    // True if the intent passed qualification; unqualified intents are excluded.
    bool qualified = false;
};

struct PaigoritResult {
    GuideIntent const* best = nullptr;
    double bestScore = 0.0;
    double bestRawFuzzy = 0.0;
    bool ambiguous = false;
    GuideIntent const* runnerUp = nullptr; // second qualified intent, when ambiguous
    std::vector<ScoredIntent> ranking;     // filtered to qualified, sorted
    // Best functional near-misses (below the floor but plausible), used to build
    // a helpful "did you mean ...?" fallback instead of a static message.
    std::vector<GuideIntent const*> suggestions;
};

class PaigoritV1 {
public:
    // Minimum fuzzy score for a token-anchored match to qualify an intent.
    static constexpr double kMatchFloor = 70.0;

    // Higher floor for conversational intents to avoid false positives on technical queries.
    static constexpr double kMatchFloorConversational = 85.0;

    // For >=4-word queries, conversational intents require a near-perfect match.
    static constexpr double kMatchFloorConversationalLong = 92.0;

    // Token-level fuzzy at/above this counts as a real match (typo tolerance),
    // and marks the match as "anchored" so it can qualify at the normal floor.
    static constexpr double kTokenAnchor = 80.0;

    // Phrase-level partial/token_set with no token anchor must reach this to
    // qualify; keeps short keywords from matching unrelated sentences.
    static constexpr double kPhraseFloor = 88.0;

    // If the top two finalScores differ by less than this (same tier), mark ambiguous.
    static constexpr double kAmbiguityGap = 6.0;

    // Quality factor maps fuzzy (floor..100) onto [kQualityBase .. kQualityBase+kQualityRange].
    static constexpr double kQualityBase = 0.65;
    static constexpr double kQualityRange = 0.35;

    // Max bonus from query coverage (how much of the query the intent explains).
    static constexpr double kCoverageBonusMax = 15.0;

    // Functional intents scoring at least this (but unqualified) are offered as fallback suggestions.
    static constexpr double kSuggestionFloor = 45.0;

    static PaigoritResult run(std::vector<GuideIntent> const& intents,
                              std::string const& normalizedQuery,
                              std::vector<std::string> const& queryTokens,
                              std::string const& langId);

    // Detect a multi-topic query ("cursor and discord"): split on conjunctions and
    // return the distinct strong (tier>=3) functional intents per segment, in order
    // (max 3). Returns empty if there's no conjunction or fewer than two topics.
    static std::vector<GuideIntent const*> splitTopics(
        std::vector<GuideIntent> const& intents,
        std::string const& normalizedQuery,
        std::string const& langId);

private:
    // Fuzzy match of one keyword against the query. Returns the best score (0-100)
    // and whether it came from token-level evidence (anchored) vs phrase-level partial.
    struct KwMatch { double score = 0.0; double anchoredScore = 0.0; };
    static KwMatch matchKeyword(std::string const& normalizedQuery,
                                std::vector<std::string> const& expandedTokens,
                                std::string const& keyword);

    // True if a multi-word keyword appears as a contiguous run of tokens (after synonym/stem).
    static bool keywordAppearsAsCompound(std::vector<std::vector<std::string>> const& tokenForms,
                                         std::vector<std::string> const& kwTokens);

    // True if any token form exactly matches the keyword.
    static bool anyTokenFormEquals(std::vector<std::vector<std::string>> const& tokenForms,
                                   std::string const& keyword);

    // Marks, in `covered`, which query tokens any token of the keyword matches.
    static void markCoveredTokens(std::vector<std::vector<std::string>> const& tokenForms,
                                  std::vector<std::string> const& kwTokens,
                                  std::vector<bool>& covered);
};

} // namespace paimon::guide
