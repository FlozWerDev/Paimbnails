#pragma once

#include <string>
#include <vector>
#include <unordered_set>
#include <unordered_map>

// Stopword removal + suffix stemming + synonym map for Paigorit V1; no external deps.

namespace paimon::guide {

class LightLemmatizer {
public:
    // True if the token is a stopword (any supported language).
    static bool isStopword(std::string const& tokenLower);

    // Basic stemming: trims common EN/ES suffixes. Tokens < 4 chars are returned as-is.
    static std::string stem(std::string const& tokenLower);

    // Expand a token to deduplicated canonical forms (synonym + stem); empty for stopwords.
    static std::vector<std::string> expand(std::string const& tokenLower);

    // Filter stopwords from a token list.
    static std::vector<std::string> removeStopwords(std::vector<std::string> const& tokens);

private:
    // Static shared EN/ES stopword table.
    static std::unordered_set<std::string> const& stopwords();

    // Static synonym table: key -> canonical value.
    static std::unordered_map<std::string, std::string> const& synonyms();
};

} // namespace paimon::guide
