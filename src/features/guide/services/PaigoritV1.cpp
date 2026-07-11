#include "PaigoritV1.hpp"
#include "LightLemmatizer.hpp"

#include <rapidfuzz/fuzz.hpp>
#include <algorithm>
#include <cctype>

namespace paimon::guide {

namespace {

std::string normalizeKeyword(std::string s) {
    std::string out;
    out.reserve(s.size());
    bool lastSpace = true;
    for (char c : s) {
        unsigned char u = static_cast<unsigned char>(c);
        if (u < 0x80) {
            char low = static_cast<char>(std::tolower(u));
            if (std::isalnum(static_cast<unsigned char>(low))) {
                out.push_back(low);
                lastSpace = false;
            } else {
                if (!lastSpace) {
                    out.push_back(' ');
                    lastSpace = true;
                }
            }
        } else {
            out.push_back(c);
            lastSpace = false;
        }
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

std::vector<std::string> tokenizeKw(std::string const& s) {
    std::vector<std::string> tokens;
    std::string cur;
    for (char c : s) {
        if (c == ' ') {
            if (!cur.empty()) { tokens.push_back(cur); cur.clear(); }
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) tokens.push_back(cur);
    return tokens;
}

std::vector<std::vector<std::string>> buildTokenForms(
    std::vector<std::string> const& filteredTokens)
{
    std::vector<std::vector<std::string>> result;
    result.reserve(filteredTokens.size());
    for (auto const& t : filteredTokens) {
        auto forms = LightLemmatizer::expand(t);
        if (forms.empty()) forms.push_back(t);
        result.push_back(std::move(forms));
    }
    return result;
}

bool tokensSimilar(std::string const& a, std::string const& b) {
    if (a == b) return true;
    if (a.size() < 3 || b.size() < 3) return false;
    auto sa = LightLemmatizer::stem(a);
    auto sb = LightLemmatizer::stem(b);
    if (sa == sb && !sa.empty()) return true;
    double r = rapidfuzz::fuzz::ratio(a, b);
    return r >= 85.0;
}

} // namespace

PaigoritV1::KwMatch PaigoritV1::matchKeyword(
    std::string const& normalizedQuery,
    std::vector<std::string> const& expandedTokens,
    std::string const& keyword)
{
    KwMatch m;
    if (keyword.empty()) return m;

    // Phrase-level: token_set + partial against the whole query. Catches keywords
    // embedded in a sentence, but it's easy to inflate on short keywords (a 2-char
    // alias matching inside a longer word), so it's skipped for short keywords and
    // never anchors the match.
    if (keyword.size() >= 4) {
        double tokenSet = rapidfuzz::fuzz::token_set_ratio(normalizedQuery, keyword);
        double partial  = rapidfuzz::fuzz::partial_ratio(normalizedQuery, keyword);
        if (keyword.size() >= 5 && normalizedQuery.find(keyword) != std::string::npos) {
            partial = std::max(partial, 95.0);
        }
        m.score = std::max(tokenSet, partial);
    }

    // Token-level: compare single-word keywords against each expanded query form.
    // Exact/stem/synonym/typo hits here are trustworthy, so they anchor the match.
    auto kwTokens = tokenizeKw(keyword);
    if (kwTokens.size() == 1) {
        std::string const& kw = kwTokens[0];
        std::string kwStem = LightLemmatizer::stem(kw);
        for (auto const& tok : expandedTokens) {
            double tokenScore = 0.0;
            if (tok == kw) {
                tokenScore = 100.0;
            } else if (!kwStem.empty() && LightLemmatizer::stem(tok) == kwStem) {
                tokenScore = 95.0;
            } else {
                tokenScore = rapidfuzz::fuzz::ratio(tok, kw);
            }
            m.score = std::max(m.score, tokenScore);
            if (tokenScore >= kTokenAnchor) {
                m.anchoredScore = std::max(m.anchoredScore, tokenScore);
            }
        }
    }

    return m;
}

// Compound matching (multi-word keyword as a contiguous run or bag-of-words)

bool PaigoritV1::keywordAppearsAsCompound(
    std::vector<std::vector<std::string>> const& tokenForms,
    std::vector<std::string> const& kwTokens)
{
    if (kwTokens.size() < 2 || tokenForms.size() < kwTokens.size()) return false;

    // 1) Contiguous in-order match (ideal: "profile background" -> "profile background")
    for (size_t i = 0; i + kwTokens.size() <= tokenForms.size(); ++i) {
        bool allMatch = true;
        for (size_t j = 0; j < kwTokens.size(); ++j) {
            bool any = false;
            for (auto const& form : tokenForms[i + j]) {
                if (tokensSimilar(form, kwTokens[j])) { any = true; break; }
            }
            if (!any) { allMatch = false; break; }
        }
        if (allMatch) return true;
    }

    // 2) Bag-of-words match: all keyword words appear in the query in any order.
    std::vector<bool> consumed(tokenForms.size(), false);
    for (auto const& kwt : kwTokens) {
        bool found = false;
        for (size_t k = 0; k < tokenForms.size(); ++k) {
            if (consumed[k]) continue;
            for (auto const& form : tokenForms[k]) {
                if (tokensSimilar(form, kwt)) { found = true; consumed[k] = true; break; }
            }
            if (found) break;
        }
        if (!found) return false;
    }
    return true;
}

bool PaigoritV1::anyTokenFormEquals(
    std::vector<std::vector<std::string>> const& tokenForms,
    std::string const& keyword)
{
    for (auto const& forms : tokenForms) {
        for (auto const& f : forms) {
            if (f == keyword) return true;
        }
    }
    return false;
}

void PaigoritV1::markCoveredTokens(
    std::vector<std::vector<std::string>> const& tokenForms,
    std::vector<std::string> const& kwTokens,
    std::vector<bool>& covered)
{
    for (size_t i = 0; i < tokenForms.size(); ++i) {
        if (covered[i]) continue;
        for (auto const& form : tokenForms[i]) {
            bool hit = false;
            for (auto const& kwt : kwTokens) {
                if (tokensSimilar(form, kwt)) { hit = true; break; }
            }
            if (hit) { covered[i] = true; break; }
        }
    }
}

// Run: the matcher core

PaigoritResult PaigoritV1::run(std::vector<GuideIntent> const& intents,
                                std::string const& normalizedQuery,
                                std::vector<std::string> const& queryTokens,
                                std::string const& langId)
{
    PaigoritResult result;

    // 1) Drop stopwords and build expanded forms for each token.
    auto filteredTokens = LightLemmatizer::removeStopwords(queryTokens);
    auto tokenForms = buildTokenForms(filteredTokens);

    // Flat list of all expanded forms (for single-word keyword matching).
    std::vector<std::string> flatForms;
    for (auto const& forms : tokenForms) {
        for (auto const& f : forms) flatForms.push_back(f);
    }

    int relevantCount = static_cast<int>(filteredTokens.size());

    // 2) Score each intent.
    std::vector<ScoredIntent> all;
    all.reserve(intents.size());

    for (auto const& intent : intents) {
        auto kwIt = intent.keywordsByLang.find(langId);
        if (kwIt == intent.keywordsByLang.end()) {
            kwIt = intent.keywordsByLang.find("english");
        }
        if (kwIt == intent.keywordsByLang.end()) continue;

        // Normalize keywords
        std::vector<std::string> normalizedKeywords;
        normalizedKeywords.reserve(kwIt->second.size());
        for (auto const& kwRaw : kwIt->second) {
            auto kw = normalizeKeyword(kwRaw);
            if (!kw.empty()) normalizedKeywords.push_back(std::move(kw));
        }
        if (normalizedKeywords.empty()) continue;

        ScoredIntent scored;
        scored.intent = &intent;

        // Track which query tokens this intent explains (for coverage).
        std::vector<bool> covered(tokenForms.size(), false);

        // For each keyword: fuzzy/anchor score + compound/exact/coverage.
        for (auto const& kw : normalizedKeywords) {
            if (kw == normalizedQuery) scored.hasFullExactMatch = true;
            auto km = matchKeyword(normalizedQuery, flatForms, kw);
            scored.bestKeywordFuzzy = std::max(scored.bestKeywordFuzzy, km.score);
            scored.bestAnchoredFuzzy = std::max(scored.bestAnchoredFuzzy, km.anchoredScore);

            auto kwTokens = tokenizeKw(kw);
            if (kwTokens.size() >= 2) {
                if (keywordAppearsAsCompound(tokenForms, kwTokens)) {
                    scored.hasCompoundMatch = true;
                    scored.bestAnchoredFuzzy = std::max(scored.bestAnchoredFuzzy, 100.0);
                    scored.bestKeywordFuzzy = std::max(scored.bestKeywordFuzzy, 100.0);
                }
            } else if (!kwTokens.empty()) {
                if (anyTokenFormEquals(tokenForms, kwTokens[0])) {
                    scored.hasExactTokenMatch = true;
                }
            }
            markCoveredTokens(tokenForms, kwTokens, covered);
        }

        // Soft search phrases (problem / how-to language). Cap so they never beat
        // a strong keyword match on another intent. Can still qualify the intent.
        auto spIt = intent.searchPhrasesByLang.find(langId);
        if (spIt == intent.searchPhrasesByLang.end()) {
            spIt = intent.searchPhrasesByLang.find("english");
        }
        if (spIt != intent.searchPhrasesByLang.end()) {
            for (auto const& raw : spIt->second) {
                auto phrase = normalizeKeyword(raw);
                if (phrase.empty()) continue;
                auto km = matchKeyword(normalizedQuery, flatForms, phrase);
                double capped = std::min(km.score, kSearchPhraseCap);
                scored.bestSearchFuzzy = std::max(scored.bestSearchFuzzy, capped);

                auto pToks = tokenizeKw(phrase);
                if (pToks.size() >= 2 && keywordAppearsAsCompound(tokenForms, pToks)) {
                    scored.bestSearchFuzzy = std::max(scored.bestSearchFuzzy, kSearchPhraseCap);
                    markCoveredTokens(tokenForms, pToks, covered);
                } else {
                    markCoveredTokens(tokenForms, pToks, covered);
                }
            }
            if (scored.bestSearchFuzzy >= kSearchPhraseFloor) {
                scored.hasSearchPhraseMatch = true;
                // Fold into the keyword fuzzy used for ranking, but never above the cap.
                scored.bestKeywordFuzzy = std::max(scored.bestKeywordFuzzy, scored.bestSearchFuzzy);
                // Search phrases can anchor qualification when they are solid.
                if (scored.bestSearchFuzzy >= kSearchPhraseFloor) {
                    scored.bestAnchoredFuzzy = std::max(
                        scored.bestAnchoredFuzzy,
                        std::min(scored.bestSearchFuzzy, 88.0));
                }
            }
        }

        // Description tokens: only coverage/desempate, never qualify alone.
        auto descIt = intent.descriptionByLang.find(langId);
        if (descIt == intent.descriptionByLang.end()) {
            descIt = intent.descriptionByLang.find("english");
        }
        if (descIt != intent.descriptionByLang.end() && !tokenForms.empty()) {
            auto descNorm = normalizeKeyword(descIt->second);
            auto descToks = LightLemmatizer::removeStopwords(tokenizeKw(descNorm));
            if (!descToks.empty()) {
                int dHit = 0;
                for (auto const& forms : tokenForms) {
                    bool hit = false;
                    for (auto const& f : forms) {
                        for (auto const& dt : descToks) {
                            if (tokensSimilar(f, dt)) { hit = true; break; }
                        }
                        if (hit) break;
                    }
                    if (hit) ++dHit;
                }
                scored.descriptionCoverage =
                    static_cast<double>(dHit) / tokenForms.size();
            }
        }

        if (!tokenForms.empty()) {
            int hit = 0;
            for (bool c : covered) if (c) ++hit;
            scored.coverageRatio = static_cast<double>(hit) / tokenForms.size();
        }

        // Pick the floor based on intent kind and query length.
        double floor = kMatchFloor;
        if (intent.kind == IntentKind::Conversational) {
            floor = (relevantCount >= 4)
                ? kMatchFloorConversationalLong
                : kMatchFloorConversational;
        }

        // Qualify on token-level evidence at the normal floor, OR on a strong
        // phrase-level match (>= kPhraseFloor). Phrase-only weak matches are dropped.
        // Search-phrase matches also qualify when above kSearchPhraseFloor.
        bool anchoredQual = scored.bestAnchoredFuzzy >= floor;
        bool phraseQual = scored.bestKeywordFuzzy >= std::max(floor, kPhraseFloor);
        bool searchQual = scored.hasSearchPhraseMatch
                          && scored.bestSearchFuzzy >= kSearchPhraseFloor;
        scored.qualified = anchoredQual || phraseQual || searchQual;

        // Match-quality tier. Token-level certainty (exact word / compound) ranks
        // above strong-but-fuzzy phrase matches, which rank above weak ones, so a
        // perfect match to a light intent beats a heavy intent matched by substring.
        // Search-phrase-only hits are capped at tier 2.
        bool keywordStrong = scored.hasFullExactMatch
            || scored.hasExactTokenMatch
            || scored.hasCompoundMatch
            || scored.bestAnchoredFuzzy >= 90.0
            || (!scored.hasSearchPhraseMatch && scored.bestKeywordFuzzy >= 97.0);

        if (scored.hasFullExactMatch) {
            scored.tier = 4;
        } else if (scored.hasExactTokenMatch || scored.hasCompoundMatch) {
            scored.tier = 3;
        } else if (scored.bestAnchoredFuzzy >= 90.0 || scored.bestKeywordFuzzy >= 97.0) {
            scored.tier = 2;
        } else if (scored.hasSearchPhraseMatch && !keywordStrong) {
            // Soft problem-phrase route: solid but not name-level certainty.
            scored.tier = (scored.bestSearchFuzzy >= 90.0) ? 2 : 1;
        } else if (scored.bestAnchoredFuzzy >= kTokenAnchor
                   || scored.bestKeywordFuzzy >= kPhraseFloor) {
            scored.tier = 1;
        } else {
            scored.tier = 0;
        }

        // Confidence bonus from compound / exact / high fuzzy / coverage.
        if (scored.hasCompoundMatch) scored.confidenceBonus += 20.0;
        if (scored.hasExactTokenMatch) scored.confidenceBonus += 10.0;
        if (scored.bestKeywordFuzzy >= 95.0) scored.confidenceBonus += 5.0;
        if (scored.hasSearchPhraseMatch) scored.confidenceBonus += 6.0;
        scored.confidenceBonus += scored.coverageRatio * kCoverageBonusMax;
        scored.confidenceBonus += scored.descriptionCoverage * 4.0; // small desempate

        // Quality factor scales the curated weight by match strength (used only
        // to order intents within the same tier).
        double span = std::max(1.0, 100.0 - floor);
        double norm = std::clamp((scored.bestKeywordFuzzy - floor) / span, 0.0, 1.0);
        double qualityFactor = kQualityBase + kQualityRange * norm;

        scored.finalScore = static_cast<double>(intent.weight) * qualityFactor
                          + scored.confidenceBonus;

        all.push_back(scored);
    }

    // 3) Keep qualified intents and sort by tier, then within-tier finalScore.
    for (auto const& s : all) {
        if (s.qualified) result.ranking.push_back(s);
    }
    std::sort(result.ranking.begin(), result.ranking.end(),
              [](ScoredIntent const& a, ScoredIntent const& b) {
                  if (a.tier != b.tier) return a.tier > b.tier;
                  if (a.finalScore != b.finalScore)
                      return a.finalScore > b.finalScore;
                  if (a.coverageRatio != b.coverageRatio)
                      return a.coverageRatio > b.coverageRatio;
                  return a.bestKeywordFuzzy > b.bestKeywordFuzzy;
              });

    if (result.ranking.empty()) {
        // Nothing qualified: surface the closest functional near-misses so the
        // caller can ask "did you mean ...?" instead of a static fallback.
        std::vector<ScoredIntent> nearMisses;
        for (auto const& s : all) {
            if (s.intent->kind == IntentKind::Functional
                && s.bestKeywordFuzzy >= kSuggestionFloor) {
                nearMisses.push_back(s);
            }
        }
        std::sort(nearMisses.begin(), nearMisses.end(),
                  [](ScoredIntent const& a, ScoredIntent const& b) {
                      return a.bestKeywordFuzzy > b.bestKeywordFuzzy;
                  });
        for (std::size_t i = 0; i < nearMisses.size() && i < 2; ++i) {
            result.suggestions.push_back(nearMisses[i].intent);
        }
        return result;
    }

    auto const& top = result.ranking.front();
    result.best = top.intent;
    result.bestScore = top.finalScore;
    result.bestRawFuzzy = top.bestKeywordFuzzy;

    if (result.ranking.size() >= 2) {
        auto const& second = result.ranking[1];
        double gap = top.finalScore - second.finalScore;
        if (top.tier == second.tier && gap < kAmbiguityGap) {
            result.ambiguous = true;
            result.runnerUp = second.intent;
        }
    }

    return result;
}

std::vector<GuideIntent const*> PaigoritV1::splitTopics(
    std::vector<GuideIntent> const& intents,
    std::string const& normalizedQuery,
    std::string const& langId)
{
    auto toks = tokenizeKw(normalizedQuery);

    auto isConj = [](std::string const& t) {
        return t == "and" || t == "y" || t == "e";
    };

    bool hasConj = false;
    for (auto const& t : toks) if (isConj(t)) { hasConj = true; break; }
    if (!hasConj) return {};

    // Build segments split on conjunction tokens.
    std::vector<std::string> segments;
    std::string cur;
    for (auto const& t : toks) {
        if (isConj(t)) {
            if (!cur.empty()) { cur.pop_back(); segments.push_back(cur); cur.clear(); }
        } else {
            cur += t;
            cur.push_back(' ');
        }
    }
    if (!cur.empty()) { cur.pop_back(); segments.push_back(cur); }

    std::vector<GuideIntent const*> hits;
    for (auto const& seg : segments) {
        if (seg.empty()) continue;
        auto segToks = tokenizeKw(seg);
        auto res = run(intents, seg, segToks, langId);
        if (!res.best || res.ranking.empty()) continue;
        if (res.best->kind != IntentKind::Functional) continue;
        if (res.ranking.front().tier < 3) continue;

        bool dup = false;
        for (auto const* h : hits) if (h->id == res.best->id) { dup = true; break; }
        if (!dup) hits.push_back(res.best);
        if (hits.size() >= 3) break;
    }

    if (hits.size() < 2) return {};
    return hits;
}

} // namespace paimon::guide
