#pragma once

#include "GuideIntents.hpp"
#include <string>
#include <vector>
#include <unordered_map>

// ranks qualified intents by fuzzy quality, exactness, coverage, and weight.

namespace paimon::guide {

struct ScoredIntent {
    GuideIntent const* intent = nullptr;

    // best keyword score, including phrase matches.
    double bestKeywordFuzzy = 0.0;

    // token-level score; phrase partials do not count.
    double bestAnchoredFuzzy = 0.0;

    // problem/how-to phrase score, capped below keyword matches.
    double bestSearchFuzzy = 0.0;
    bool hasSearchPhraseMatch = false;

    // description-token tie-breaker.
    double descriptionCoverage = 0.0;

    bool hasCompoundMatch = false;

    bool hasExactTokenMatch = false;

    bool hasFullExactMatch = false;

    // fraction of query tokens explained by this intent.
    double coverageRatio = 0.0;

    // tier 4 exact, 3 compound/token, 2 strong fuzzy, 1 typo, 0 weak.
    int tier = 0;

    // confidence from exactness, fuzziness, and coverage.
    double confidenceBonus = 0.0;

    // within-tier score: weight × quality + confidence.
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
    // functional near-misses for "did you mean?" suggestions.
    std::vector<GuideIntent const*> suggestions;
};

class PaigoritV1 {
public:
    // fuzzy thresholds (0..100).
    static constexpr double kMatchFloor = 70.0;

    static constexpr double kMatchFloorConversational = 85.0;

    static constexpr double kMatchFloorConversationalLong = 92.0;

    static constexpr double kTokenAnchor = 80.0;

    static constexpr double kPhraseFloor = 88.0;

    // same-tier score gap below which results are ambiguous.
    static constexpr double kAmbiguityGap = 6.0;

    // map fuzzy scores to the quality-factor range.
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

    // split conjunctions into up to three strong functional topics.
    static std::vector<GuideIntent const*> splitTopics(
        std::vector<GuideIntent> const& intents,
        std::string const& normalizedQuery,
        std::string const& langId);

private:
    // match one keyword and return phrase and anchored scores.
    struct KwMatch { double score = 0.0; double anchoredScore = 0.0; };
    static KwMatch matchKeyword(std::string const& normalizedQuery,
                                std::vector<std::string> const& expandedTokens,
                                std::string const& keyword);

    // whether a multi-word keyword appears as a contiguous token run.
    static bool keywordAppearsAsCompound(std::vector<std::vector<std::string>> const& tokenForms,
                                         std::vector<std::string> const& kwTokens);

    // whether any token form equals the keyword.
    static bool anyTokenFormEquals(std::vector<std::vector<std::string>> const& tokenForms,
                                   std::string const& keyword);

    // mark query tokens covered by a keyword.
    static void markCoveredTokens(std::vector<std::vector<std::string>> const& tokenForms,
                                  std::vector<std::string> const& kwTokens,
                                  std::vector<bool>& covered);
};

}
