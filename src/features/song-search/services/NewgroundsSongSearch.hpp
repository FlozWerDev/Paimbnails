#pragma once

#include <functional>
#include <string>

namespace paimon::songsearch {

enum class SearchStatus {
    Found,
    NoResults,
    NetworkError
};

struct SearchResult {
    SearchStatus status = SearchStatus::NoResults;
    std::string songID;   // valid only when status == found
};

bool isNumericID(std::string const& text);
void resolveByName(std::string const& rawQuery, std::function<void(SearchResult)> callback);

} // namespace paimon::songsearch
