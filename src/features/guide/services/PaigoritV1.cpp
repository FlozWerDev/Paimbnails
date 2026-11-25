#include "PaigoritV1.hpp"
#include "LightLemmatizer.hpp"
#include "MicroRanker.hpp"

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

}

PaigoritV1::KwMatch PaigoritV1::matchKeyword(
    std::string const& normalizedQuery,
    std::vector<std::string> const& expandedTokens,
    std::string const& keyword)
{
    KwMatch m;
    if (keyword.empty()) return m;

    // phrase matching catches embedded keywords but never anchors short aliases.
    if (keyword.size() >= 4) {
        double tokenSet = rapidfuzz::fuzz::token_set_ratio(normalizedQuery, keyword);
        double partial  = rapidfuzz::fuzz::partial_ratio(normalizedQuery, keyword);
        if (keyword.size() >= 5 && normalizedQuery.find(keyword) != std::string::npos) {
            partial = std::max(partial, 95.0);
        }
        m.score = std::max(tokenSet, partial);
    }

    // token matching uses expanded forms; exact/stem/synonym/typo hits anchor.
    auto kwTokens = LightLemmatizer::tokenize(keyword);
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

// match compound keywords contiguously, then as an unordered bag of words.

bool PaigoritV1::keywordAppearsAsCompound(
    std::vector<std::vector<std::string>> const& tokenForms,
    std::vector<std::string> const& kwTokens)
{
    if (kwTokens.size() < 2 || tokenForms.size() < kwTokens.size()) return false;

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

PaigoritResult PaigoritV1::run(std::vector<GuideIntent> const& intents,
                                std::string const& normalizedQuery,
                                std::vector<std::string> const& queryTokens,
                                std::string const& langId)
{
    PaigoritResult result;

    auto filteredTokens = LightLemmatizer::removeStopwords(queryTokens);
    auto tokenForms = buildTokenForms(filteredTokens);

    std::vector<std::string> flatForms;
    for (auto const& forms : tokenForms) {
        for (auto const& f : forms) flatForms.push_back(f);
    }

    int relevantCount = static_cast<int>(filteredTokens.size());

    std::vector<ScoredIntent> all;
    all.reserve(intents.size());

    for (auto const& intent : intents) {
        // bilingual matching: score keywords in the active language PLUS the
        // other supported language. Players constantly mix "hola" into an
        // english UI (and vice versa); matching a single language made those
        // queries fall back to random "did you mean?" suggestions.
        std::vector<std::string> normalizedKeywords;
        {
            std::string const* langs[3] = {&langId, nullptr, nullptr};
            std::string other = (langId == "spanish") ? "english" : "spanish";
            langs[1] = &other;
            static std::string const kEn = "english";
            langs[2] = &kEn;
            bool seenEn = false, seenEs = false, seenReq = false;
            for (auto const* lp : langs) {
                if (!lp || lp->empty()) continue;
                if (*lp == "english" && seenEn) continue;
                if (*lp == "spanish" && seenEs) continue;
                if (*lp == langId && seenReq) continue;
                if (*lp == "english") seenEn = true;
                else if (*lp == "spanish") seenEs = true;
                else seenReq = true;
                auto kwIt = intent.keywordsByLang.find(*lp);
                if (kwIt == intent.keywordsByLang.end()) continue;
                for (auto const& kwRaw : kwIt->second) {
                    auto kw = normalizeKeyword(kwRaw);
                    if (kw.empty()) continue;
                    if (std::find(normalizedKeywords.begin(), normalizedKeywords.end(), kw)
                        == normalizedKeywords.end()) {
                        normalizedKeywords.push_back(std::move(kw));
                    }
                }
            }
        }
        if (normalizedKeywords.empty()) continue;

        ScoredIntent scored;
        scored.intent = &intent;

        std::vector<bool> covered(tokenForms.size(), false);

        // score keyword matches and mark covered query tokens.
        for (auto const& kw : normalizedKeywords) {
            if (kw == normalizedQuery) scored.hasFullExactMatch = true;
            auto km = matchKeyword(normalizedQuery, flatForms, kw);
            scored.bestKeywordFuzzy = std::max(scored.bestKeywordFuzzy, km.score);
            scored.bestAnchoredFuzzy = std::max(scored.bestAnchoredFuzzy, km.anchoredScore);

            auto kwTokens = LightLemmatizer::tokenize(kw);
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

        // search phrases stay below strong keyword matches but can qualify.
        // same bilingual rule as keywords: check both languages.
        {
            std::vector<std::string> phraseRaws;
            {
                std::string other = (langId == "spanish") ? "english" : "spanish";
                std::string const* langs[3] = {&langId, &other, nullptr};
                static std::string const kEn = "english";
                langs[2] = &kEn;
                for (auto const* lp : langs) {
                    if (!lp || lp->empty()) continue;
                    auto spIt = intent.searchPhrasesByLang.find(*lp);
                    if (spIt == intent.searchPhrasesByLang.end()) continue;
                    for (auto const& r : spIt->second) phraseRaws.push_back(r);
                    if (lp == &langId) {
                        // avoid triple-adding when langId is already english/spanish
                        if (*lp == "english" || *lp == "spanish") {
                            // still allow the other language below; mark by skipping dup later
                        }
                    }
                    if (phraseRaws.size() > 64) break;
                }
            }
            for (auto const& raw : phraseRaws) {
                auto phrase = normalizeKeyword(raw);
                if (phrase.empty()) continue;
                auto km = matchKeyword(normalizedQuery, flatForms, phrase);
                double capped = std::min(km.score, kSearchPhraseCap);
                scored.bestSearchFuzzy = std::max(scored.bestSearchFuzzy, capped);

                auto pToks = LightLemmatizer::tokenize(phrase);
                if (pToks.size() >= 2 && keywordAppearsAsCompound(tokenForms, pToks)) {
                    scored.bestSearchFuzzy = std::max(scored.bestSearchFuzzy, kSearchPhraseCap);
                }
                markCoveredTokens(tokenForms, pToks, covered);
            }
            if (scored.bestSearchFuzzy >= kSearchPhraseFloor) {
                scored.hasSearchPhraseMatch = true;
                // include phrase score without exceeding the cap; strong phrases anchor.
                scored.bestKeywordFuzzy = std::max(scored.bestKeywordFuzzy, scored.bestSearchFuzzy);
                scored.bestAnchoredFuzzy = std::max(
                    scored.bestAnchoredFuzzy,
                    std::min(scored.bestSearchFuzzy, 88.0));
            }
        }

        // description tokens affect coverage only; they never qualify an intent.
        auto descIt = intent.descriptionByLang.find(langId);
        if (descIt == intent.descriptionByLang.end()) {
            descIt = intent.descriptionByLang.find("english");
        }
        if (descIt != intent.descriptionByLang.end() && !tokenForms.empty()) {
            auto descNorm = normalizeKeyword(descIt->second);
            auto descToks = LightLemmatizer::removeStopwords(LightLemmatizer::tokenize(descNorm));
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

        // pick the qualification floor from intent kind and query length.
        double floor = kMatchFloor;
        if (intent.kind == IntentKind::Conversational) {
            floor = (relevantCount >= 4)
                ? kMatchFloorConversationalLong
                : kMatchFloorConversational;
        }

        // qualify anchored matches, strong phrases, or high-confidence search phrases.
        bool anchoredQual = scored.bestAnchoredFuzzy >= floor;
        bool phraseQual = scored.bestKeywordFuzzy >= std::max(floor, kPhraseFloor);
        bool searchQual = scored.hasSearchPhraseMatch
                          && scored.bestSearchFuzzy >= kSearchPhraseFloor;
        scored.qualified = anchoredQual || phraseQual || searchQual;

        // exact/compound/token certainty outranks fuzzy phrases; search-only hits
        // are capped at tier 2.
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
            scored.tier = (scored.bestSearchFuzzy >= 90.0) ? 2 : 1;
        } else if (scored.bestAnchoredFuzzy >= kTokenAnchor
                   || scored.bestKeywordFuzzy >= kPhraseFloor) {
            scored.tier = 1;
        } else {
            scored.tier = 0;
        }

        // add confidence from exactness, fuzziness, and coverage.
        if (scored.hasCompoundMatch) scored.confidenceBonus += 20.0;
        if (scored.hasExactTokenMatch) scored.confidenceBonus += 10.0;
        if (scored.bestKeywordFuzzy >= 95.0) scored.confidenceBonus += 5.0;
        if (scored.hasSearchPhraseMatch) scored.confidenceBonus += 6.0;
        scored.confidenceBonus += scored.coverageRatio * kCoverageBonusMax;
        scored.confidenceBonus += scored.descriptionCoverage * 4.0;

        // scale curated weight by match quality within the tier.
        double span = std::max(1.0, 100.0 - floor);
        double norm = std::clamp((scored.bestKeywordFuzzy - floor) / span, 0.0, 1.0);
        double qualityFactor = kQualityBase + kQualityRange * norm;

        scored.finalScore = static_cast<double>(intent.weight) * qualityFactor
                          + scored.confidenceBonus
                          + MicroRanker::score(intent.id, normalizedQuery);

        all.push_back(scored);
    }

    // keep qualified intents, ordered by tier and then finalscore.
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
        // offer functional near-misses for a "did you mean?" fallback.
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
    auto toks = LightLemmatizer::tokenize(normalizedQuery);

    auto isConj = [](std::string const& t) {
        return t == "and" || t == "y" || t == "e" || t == "con"
            || t == "plus" || t == "mas" || t == "with";
    };

    bool hasConj = false;
    for (auto const& t : toks) if (isConj(t)) { hasConj = true; break; }

    // split the query into conjunction-separated segments.
    std::vector<std::string> segments;
    if (hasConj) {
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
    } else if (toks.size() >= 2 && toks.size() <= 6) {
        // no explicit conjunction (commas/& are stripped by normalize), but
        // "cursor discord" or "musica fondos" may still be two topics. Try every
        // split point and keep the one where both sides match strongly.
        std::vector<std::string> bestPair;
        double bestSum = 0.0;
        for (std::size_t cut = 1; cut < toks.size(); ++cut) {
            std::string left, right;
            for (std::size_t i = 0; i < cut; ++i) { left += toks[i]; left.push_back(' '); }
            for (std::size_t i = cut; i < toks.size(); ++i) { right += toks[i]; right.push_back(' '); }
            if (!left.empty()) left.pop_back();
            if (!right.empty()) right.pop_back();
            auto lToks = LightLemmatizer::tokenize(left);
            auto rToks = LightLemmatizer::tokenize(right);
            auto lRes = run(intents, left, lToks, langId);
            auto rRes = run(intents, right, rToks, langId);
            if (!lRes.best || !rRes.best) continue;
            if (lRes.best->kind != IntentKind::Functional) continue;
            if (rRes.best->kind != IntentKind::Functional) continue;
            if (lRes.ranking.empty() || rRes.ranking.empty()) continue;
            if (lRes.ranking.front().tier < 3 || rRes.ranking.front().tier < 3) continue;
            if (lRes.best->id == rRes.best->id) continue;
            double sum = lRes.bestScore + rRes.bestScore;
            if (sum > bestSum) {
                bestSum = sum;
                bestPair = {left, right};
            }
        }
        if (!bestPair.empty()) segments = std::move(bestPair);
    }
    if (segments.empty()) return {};

    std::vector<GuideIntent const*> hits;
    for (auto const& seg : segments) {
        if (seg.empty()) continue;
        auto segToks = LightLemmatizer::tokenize(seg);
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

}
