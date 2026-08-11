#pragma once

#include "GuideIntents.hpp"
#include <string>
#include <vector>
#include <unordered_map>

// Ranks qualified intents by fuzzy quality, exactness, coverage, and weight.

namespace paimon::guide {

struct ScoredIntent {
    GuideIntent const* intent = nullptr;

    // Best keyword score, including phrase matches.
    double bestKeywordFuzzy = 0.0;

    // Token-level score; phrase partials do not count.
    double bestAnchoredFuzzy = 0.0;

    // Problem/how-to phrase score, capped below keyword matches.
    double bestSearchFuzzy = 0.0;
    bool hasSearchPhraseMatch = false;

    // Description-token tie-breaker.
    double descriptionCoverage = 0.0;

    bool hasCompoundMatch = false;

    bool hasExactTokenMatch = false;

    bool hasFullExactMatch = false;

    // Fraction of query tokens explained by this intent.
    double coverageRatio = 0.0;

    // Tier 4 exact, 3 compound/token, 2 strong fuzzy, 1 typo, 0 weak.
    int tier = 0;

    // Confidence from exactness, fuzziness, and coverage.
    double confidenceBonus = 0.0;

    // Within-tier score: weight × quality + confidence.
    double finalScore = 0.0;

    bool qualified = false;
};

struct PaigoritResult {
    GuideIntent const* best = nullptr;
    double bestScore = 0.0;
    double bestRawFuzzy = 0.0;
    bool ambiguous = false;
    GuideIntent const* runnerUp = nullptr;
    std::vector<ScoredIntent> ranking;
    // Functional near-misses for "did you mean?" suggestions.
    std::vector<GuideIntent const*> suggestions;
};

class PaigoritV1 {
public:
    // Fuzzy thresholds (0..100).
    static constexpr double kMatchFloor = 70.0;

    static constexpr double kMatchFloorConversational = 85.0;

    static constexpr double kMatchFloorConversationalLong = 92.0;

    static constexpr double kTokenAnchor = 80.0;

    static constexpr double kPhraseFloor = 88.0;

    // Same-tier score gap below which results are ambiguous.
    static constexpr double kAmbiguityGap = 6.0;

    // Map fuzzy scores to the quality-factor range.
    static constexpr double kQualityBase = 0.65;
    static constexpr double kQualityRange = 0.35;

    static constexpr double kCoverageBonusMax = 15.0;

    static constexpr double kSuggestionFloor = 45.0;

    static constexpr double kSearchPhraseCap = 92.0;

    static constexpr double kSearchPhraseFloor = 82.0;

    static PaigoritResult run(std::vector<GuideIntent> const& intents,
                              std::string const& normalizedQuery,
                              std::vector<std::string> const& queryTokens,
                              std::string const& langId);

    // Split conjunctions into up to three strong functional topics.
    static std::vector<GuideIntent const*> splitTopics(
        std::vector<GuideIntent> const& intents,
        std::string const& normalizedQuery,
        std::string const& langId);

private:
    // Match one keyword and return phrase and anchored scores.
    struct KwMatch { double score = 0.0; double anchoredScore = 0.0; };
    static KwMatch matchKeyword(std::string const& normalizedQuery,
                                std::vector<std::string> const& expandedTokens,
                                std::string const& keyword);

    // Whether a multi-word keyword appears as a contiguous token run.
    static bool keywordAppearsAsCompound(std::vector<std::vector<std::string>> const& tokenForms,
                                         std::vector<std::string> const& kwTokens);

    // Whether any token form equals the keyword.
    static bool anyTokenFormEquals(std::vector<std::vector<std::string>> const& tokenForms,
                                   std::string const& keyword);

    // Mark query tokens covered by a keyword.
    static void markCoveredTokens(std::vector<std::vector<std::string>> const& tokenForms,
                                  std::vector<std::string> const& kwTokens,
                                  std::vector<bool>& covered);
};

}
