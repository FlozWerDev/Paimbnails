#pragma once

#include <functional>
#include <string>

namespace paimon::songsearch {

enum class SearchStatus {
    Found,        // a matching song was found
    NoResults,    // the search returned no usable result
    NetworkError  // the request failed (offline / blocked / timeout)
};

struct SearchResult {
    SearchStatus status = SearchStatus::NoResults;
    std::string songID;   // valid only when status == Found
};

bool isNumericID(std::string const& text);
std::string encodeQuery(std::string const& raw);
std::string buildSearchURL(std::string const& rawQuery);
std::string extractFirstAudioID(std::string const& html);

void resolveByName(std::string const& rawQuery, std::function<void(SearchResult)> callback);

} // namespace paimon::songsearch
