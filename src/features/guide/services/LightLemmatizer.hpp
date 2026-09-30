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

private:
    // static shared en/es stopword table.
    static std::unordered_set<std::string> const& stopwords();

    // static synonym table: key -> canonical value.
    static std::unordered_map<std::string, std::string> const& synonyms();
};

} // namespace paimon::guide
