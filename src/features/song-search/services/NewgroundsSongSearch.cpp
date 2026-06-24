#include "NewgroundsSongSearch.hpp"
#include "../../../utils/WebHelper.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>

using namespace geode::prelude;

namespace paimon::songsearch {

bool isNumericID(std::string const& text) {
    if (text.empty()) return false;
    return std::all_of(text.begin(), text.end(),
        [](unsigned char c) { return std::isdigit(c) != 0; });
}

std::string encodeQuery(std::string const& raw) {
    static char const* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(raw.size() * 3);
    for (unsigned char c : raw) {
        if (std::isalnum(c)) {
            out.push_back(static_cast<char>(c));
        } else if (c == ' ') {
            out.push_back('+');
        } else if (c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(hex[(c >> 4) & 0xF]);
            out.push_back(hex[c & 0xF]);
        }
    }
    return out;
}

std::string buildSearchURL(std::string const& rawQuery) {
    return "https://www.newgrounds.com/search/summary?suitabilities=etm&terms="
        + encodeQuery(rawQuery);
}

std::string extractFirstAudioID(std::string const& html) {
    static std::string const marker = "newgrounds.com/audio/listen/";
    auto pos = html.find(marker);
    if (pos == std::string::npos) return "";

    pos += marker.size();
    std::string id;
    while (pos < html.size() && std::isdigit(static_cast<unsigned char>(html[pos]))) {
        id.push_back(html[pos]);
        ++pos;
    }
    return id;
}

void resolveByName(std::string const& rawQuery, std::function<void(SearchResult)> callback) {
    auto url = buildSearchURL(rawQuery);

    auto req = web::WebRequest();
    req.timeout(std::chrono::seconds(10));
    req.userAgent("Mozilla/5.0 (compatible; Paimbnails)");

    WebHelper::dispatch(std::move(req), "GET", url,
        [callback = std::move(callback)](web::WebResponse res) {
            if (!callback) return;
            if (!res.ok()) {
                callback({SearchStatus::NetworkError, ""});
                return;
            }
            auto body = res.string().unwrapOr("");
            auto id = extractFirstAudioID(body);
            if (id.empty()) {
                callback({SearchStatus::NoResults, ""});
            } else {
                callback({SearchStatus::Found, id});
            }
        });
}

} // namespace paimon::songsearch
