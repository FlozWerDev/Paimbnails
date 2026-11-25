#pragma once

#include <string>
#include <vector>
#include <unordered_set>
#include <unordered_map>

// stopword removal + suffix stemming + synonym map for paigorit v1; no external deps.

namespace paimon::guide {

class LightLemmatizer {
public:
    // true if the token is a stopword (any supported language).
    static bool isStopword(std::string const& tokenLower);

    // basic stemming: trims common en/es suffixes. tokens < 4 chars are returned as-is.
    static std::string stem(std::string const& tokenLower);

    // expand a token to deduplicated canonical forms (synonym + stem); empty for stopwords.
    static std::vector<std::string> expand(std::string const& tokenLower);

    // filter stopwords from a token list.
    static std::vector<std::string> removeStopwords(std::vector<std::string> const& tokens);

    // lowercase, collapse spaces, and strip common es/pt/fr accents.
    static std::string normalize(std::string s);

    // split normalized text on whitespace and basic ascii punctuation.
    static std::vector<std::string> tokenize(std::string const& normalized);

private:
    // static shared en/es stopword table.
    static std::unordered_set<std::string> const& stopwords();

    // static synonym table: key -> canonical value.
    static std::unordered_map<std::string, std::string> const& synonyms();
};

} // namespace paimon::guide
