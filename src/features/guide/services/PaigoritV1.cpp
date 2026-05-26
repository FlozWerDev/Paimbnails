#include "PaigoritV1.hpp"
#include "LightLemmatizer.hpp"

#include <rapidfuzz/fuzz.hpp>
#include <algorithm>
#include <cctype>

namespace paimon::guide {

// ─────────────────────────────────────────────────────────────────────────────
// Helpers locales
// ─────────────────────────────────────────────────────────────────────────────

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

// Para cada token de la query (sin stopwords), genera la lista de formas
// equivalentes (original + stem + sinonimo + stem(sinonimo)). Asi al
// comparar contra una keyword podemos ser tolerantes a flexion.
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

// Compara dos tokens con tolerancia a flexion: matchea si son iguales,
// si comparten stem, o si la similitud Levenshtein es >= 85.
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

// ─────────────────────────────────────────────────────────────────────────────
// Comparacion: mejor score fuzzy entre la query (y sus formas) y una keyword
// ─────────────────────────────────────────────────────────────────────────────

double PaigoritV1::bestFuzzyAgainstKeyword(
    std::string const& normalizedQuery,
    std::vector<std::string> const& expandedTokens,
    std::string const& keyword)
{
    if (keyword.empty()) return 0.0;

    // 1) Match directo de la keyword contra la query completa.
    //    token_set_ratio + partial_ratio. Substring largo se sube a 95.
    double tokenSet = rapidfuzz::fuzz::token_set_ratio(normalizedQuery, keyword);
    double partial  = rapidfuzz::fuzz::partial_ratio(normalizedQuery, keyword);
    if (keyword.size() >= 5 && normalizedQuery.find(keyword) != std::string::npos) {
        partial = std::max(partial, 95.0);
    }
    double best = std::max(tokenSet, partial);

    // 2) Para keywords de UNA sola palabra, comparar contra cada forma
    //    expandida del token (esto captura sinonimos y plurales sin
    //    depender de partial_ratio).
    auto kwTokens = tokenizeKw(keyword);
    if (kwTokens.size() == 1) {
        std::string const& kw = kwTokens[0];
        for (auto const& expandedToken : expandedTokens) {
            if (expandedToken == kw) {
                best = std::max(best, 100.0);
                break;
            }
            // Stem-equivalence
            if (LightLemmatizer::stem(expandedToken) == LightLemmatizer::stem(kw)
                && !LightLemmatizer::stem(kw).empty())
            {
                best = std::max(best, 95.0);
            }
            // Levenshtein
            double r = rapidfuzz::fuzz::ratio(expandedToken, kw);
            if (r > best) best = r;
        }
    }

    return best;
}

// ─────────────────────────────────────────────────────────────────────────────
// Compound matching (keyword multi-palabra como subsecuencia contigua O bag)
// ─────────────────────────────────────────────────────────────────────────────

bool PaigoritV1::keywordAppearsAsCompound(
    std::vector<std::vector<std::string>> const& tokenForms,
    std::vector<std::string> const& kwTokens)
{
    if (kwTokens.size() < 2 || tokenForms.size() < kwTokens.size()) return false;

    // 1) Match contiguo en orden (caso ideal: "profile background" -> "profile background")
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

    // 2) Bag-of-words match: TODAS las palabras de la keyword aparecen
    //    en la query (en cualquier orden). Esto cubre "background profile"
    //    o "profile, dame el background" — el usuario menciona ambas
    //    palabras, asi que el compound aplica.
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

// ─────────────────────────────────────────────────────────────────────────────
// Run: corazon del matcher
// ─────────────────────────────────────────────────────────────────────────────

PaigoritResult PaigoritV1::run(std::vector<GuideIntent> const& intents,
                                std::string const& normalizedQuery,
                                std::vector<std::string> const& queryTokens,
                                std::string const& langId)
{
    PaigoritResult result;

    // 1) Filtrar stopwords y construir formas expandidas de cada token.
    auto filteredTokens = LightLemmatizer::removeStopwords(queryTokens);
    auto tokenForms = buildTokenForms(filteredTokens);

    // Lista plana de TODAS las formas expandidas (para matching de keywords
    // de una sola palabra).
    std::vector<std::string> flatForms;
    for (auto const& forms : tokenForms) {
        for (auto const& f : forms) flatForms.push_back(f);
    }

    int relevantCount = static_cast<int>(filteredTokens.size());

    // 2) Por cada intent, scorear.
    std::vector<ScoredIntent> all;
    all.reserve(intents.size());

    for (auto const& intent : intents) {
        auto kwIt = intent.keywordsByLang.find(langId);
        if (kwIt == intent.keywordsByLang.end()) {
            kwIt = intent.keywordsByLang.find("english");
        }
        if (kwIt == intent.keywordsByLang.end()) continue;

        // Normalizar keywords
        std::vector<std::string> normalizedKeywords;
        normalizedKeywords.reserve(kwIt->second.size());
        for (auto const& kwRaw : kwIt->second) {
            auto kw = normalizeKeyword(kwRaw);
            if (!kw.empty()) normalizedKeywords.push_back(std::move(kw));
        }
        if (normalizedKeywords.empty()) continue;

        ScoredIntent scored;
        scored.intent = &intent;

        // Para cada keyword, calcular score fuzzy y detectar compound/exact
        for (auto const& kw : normalizedKeywords) {
            double s = bestFuzzyAgainstKeyword(normalizedQuery, flatForms, kw);
            if (s > scored.bestKeywordFuzzy) scored.bestKeywordFuzzy = s;

            auto kwTokens = tokenizeKw(kw);
            if (kwTokens.size() >= 2) {
                if (keywordAppearsAsCompound(tokenForms, kwTokens)) {
                    scored.hasCompoundMatch = true;
                }
            } else if (!kwTokens.empty()) {
                if (anyTokenFormEquals(tokenForms, kwTokens[0])) {
                    scored.hasExactTokenMatch = true;
                }
            }
        }

        // Determinar floor segun kind del intent y largo de la query.
        double floor = kMatchFloor;
        if (intent.kind == IntentKind::Conversational) {
            floor = (relevantCount >= 4)
                ? kMatchFloorConversationalLong
                : kMatchFloorConversational;
        }

        // Calificar el intent solo si pasa el floor.
        if (scored.bestKeywordFuzzy < floor) {
            scored.qualified = false;
            // No lo descartamos del array todavia; lo guardamos sin
            // qualified=true para debug (luego se filtra).
        } else {
            scored.qualified = true;
        }

        // Confianza: bonus en base a compound / exact / fuzzy alto.
        if (scored.hasCompoundMatch) scored.confidenceBonus += 20.0;
        if (scored.hasExactTokenMatch) scored.confidenceBonus += 10.0;
        if (scored.bestKeywordFuzzy >= 95.0) scored.confidenceBonus += 5.0;

        // SCORE FINAL = WEIGHT del intent + confidenceBonus.
        // ESTE es el cambio clave pedido por el usuario:
        // entre dos intents calificados, gana el de mayor weight, no el
        // de mejor fuzzy.
        scored.finalScore = static_cast<double>(intent.weight)
                          + scored.confidenceBonus;

        all.push_back(scored);
    }

    // 3) Filtrar a calificados y ordenar por finalScore (desc).
    for (auto const& s : all) {
        if (s.qualified) result.ranking.push_back(s);
    }
    std::sort(result.ranking.begin(), result.ranking.end(),
              [](ScoredIntent const& a, ScoredIntent const& b) {
                  if (a.finalScore != b.finalScore)
                      return a.finalScore > b.finalScore;
                  // Tie-breaker: mejor fuzzy primero.
                  return a.bestKeywordFuzzy > b.bestKeywordFuzzy;
              });

    if (result.ranking.empty()) return result;

    auto const& top = result.ranking.front();
    result.best = top.intent;
    result.bestScore = top.finalScore;
    result.bestRawFuzzy = top.bestKeywordFuzzy;

    if (result.ranking.size() >= 2) {
        double gap = top.finalScore - result.ranking[1].finalScore;
        if (gap < kAmbiguityGap) result.ambiguous = true;
    }

    return result;
}

} // namespace paimon::guide
