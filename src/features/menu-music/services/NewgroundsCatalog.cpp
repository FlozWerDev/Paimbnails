#include "NewgroundsCatalog.hpp"

#include "MenuMusicLibrary.hpp"
#include "../../../utils/WebHelper.hpp"
#include "../../../utils/MainThreadDelay.hpp"

#include <Geode/binding/MusicDownloadManager.hpp>
#include <Geode/utils/general.hpp>
#include <Geode/utils/web.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <fmt/format.h>

using namespace geode::prelude;

namespace paimon::menumusic {
namespace {

constexpr char const* kBrowserUserAgent =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
    "AppleWebKit/537.36 Chrome/126 Safari/537.36";

std::string trim(std::string value) {
    auto isSpace = [](unsigned char c) { return std::isspace(c) != 0; };
    while (!value.empty() && isSpace(static_cast<unsigned char>(value.front()))) {
        value.erase(value.begin());
    }
    while (!value.empty() && isSpace(static_cast<unsigned char>(value.back()))) {
        value.pop_back();
    }
    return value;
}

void replaceAll(std::string& text, std::string const& from, std::string const& to) {
    std::size_t pos = 0;
    while ((pos = text.find(from, pos)) != std::string::npos) {
        text.replace(pos, from.size(), to);
        pos += to.size();
    }
}

std::string decodeHtml(std::string value) {
    replaceAll(value, "&quot;", "\"");
    replaceAll(value, "&#039;", "'");
    replaceAll(value, "&#39;", "'");
    replaceAll(value, "&lt;", "<");
    replaceAll(value, "&gt;", ">");
    replaceAll(value, "&amp;", "&");
    return trim(std::move(value));
}

std::string between(
    std::string const& text,
    std::string const& begin,
    std::string const& end,
    std::size_t start = 0
) {
    auto beginPos = text.find(begin, start);
    if (beginPos == std::string::npos) return {};
    beginPos += begin.size();
    auto endPos = text.find(end, beginPos);
    if (endPos == std::string::npos) return {};
    return text.substr(beginPos, endPos - beginPos);
}

int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::string urlDecode(std::string const& text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '%' && i + 2 < text.size()) {
            auto hi = hexValue(text[i + 1]);
            auto lo = hexValue(text[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back(static_cast<char>(hi * 16 + lo));
                i += 2;
                continue;
            }
        }
        out.push_back(text[i] == '+' ? ' ' : text[i]);
    }
    return out;
}

std::string urlEncode(std::string const& text) {
    static char const* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(text.size() * 3);
    for (unsigned char c : text) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back(static_cast<char>(c));
        } else if (c == ' ') {
            out.push_back('+');
        } else {
            out.push_back('%');
            out.push_back(hex[(c >> 4) & 0xF]);
            out.push_back(hex[c & 0xF]);
        }
    }
    return out;
}

int parseDigitsAt(std::string const& text, std::size_t pos) {
    std::string digits;
    while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos]))) {
        digits.push_back(text[pos]);
        ++pos;
    }
    if (digits.empty() || digits.size() > 9) return 0;
    auto parsed = geode::utils::numFromString<int>(digits);
    return parsed.isOk() && parsed.unwrap() > 0 ? parsed.unwrap() : 0;
}

// Session cache: songId -> track (gdAvailable=false entries cache misses too).
std::unordered_map<int, NewgroundsTrack>& infoCache() {
    static std::unordered_map<int, NewgroundsTrack> cache;
    return cache;
}

std::optional<NewgroundsTrack> parseSongInfoResponse(int songId, std::string const& body) {
    if (body.empty() || body.front() == '-' || body.find("~|~") == std::string::npos) {
        return std::nullopt;
    }

    std::unordered_map<std::string, std::string> fields;
    std::size_t pos = 0;
    std::string key;
    bool isKey = true;
    while (pos <= body.size()) {
        auto next = body.find("~|~", pos);
        auto piece = body.substr(pos, next == std::string::npos ? next : next - pos);
        if (isKey) key = piece;
        else fields[key] = piece;
        isKey = !isKey;
        if (next == std::string::npos) break;
        pos = next + 3;
    }

    NewgroundsTrack track;
    track.songId = songId;
    track.title = fields.count("2") ? fields["2"] : "";
    track.artist = fields.count("4") ? fields["4"] : "";
    if (fields.count("5")) {
        auto size = geode::utils::numFromString<float>(fields["5"]);
        if (size.isOk()) track.sizeMb = size.unwrap();
    }
    if (fields.count("10")) {
        auto url = urlDecode(fields["10"]);
        if (url.starts_with("http://") || url.starts_with("https://")) {
            track.streamUrl = std::move(url);
        }
    }
    track.gdAvailable = true;
    return track;
}

void fetchSongInfoUncached(int songId, NewgroundsSongCallback callback) {
    auto request = web::WebRequest();
    request.timeout(std::chrono::seconds(10));
    request.userAgent("");
    request.header("Content-Type", "application/x-www-form-urlencoded");
    request.bodyString(fmt::format("songID={}&secret=Wmfd2893gb7", songId));

    WebHelper::dispatch(std::move(request), "POST",
        "https://www.boomlings.com/database/getGJSongInfo.php",
        [songId, callback = std::move(callback)](web::WebResponse response) mutable {
            NewgroundsSongResult result;
            result.track.songId = songId;
            if (!response.ok()) {
                result.error = "GD's song servers are unreachable right now.";
                if (callback) callback(std::move(result));
                return;
            }

            auto parsed = parseSongInfoResponse(songId, response.string().unwrapOr(""));
            if (!parsed) {
                // Negative-cache so hydration doesn't re-ask this session.
                infoCache().insert_or_assign(songId, result.track);
                result.error = "This song is not registered on GD's servers.";
                if (callback) callback(std::move(result));
                return;
            }

            result.success = true;
            result.track = std::move(*parsed);
            infoCache().insert_or_assign(songId, result.track);
            if (callback) callback(std::move(result));
        });
}

struct SeedTrack {
    int songId = 0;
    std::string title;
};

// Fetches GD info for every seed in parallel and returns the merged list in
// the original order. Seeds that GD doesn't know keep their feed title and
// stay gdAvailable=false.
void hydrateSeeds(
    std::vector<SeedTrack> seeds,
    std::string listTitle,
    NewgroundsListCallback callback
) {
    if (seeds.empty()) {
        NewgroundsListResult result;
        result.error = "No songs found.";
        result.listTitle = std::move(listTitle);
        if (callback) callback(std::move(result));
        return;
    }

    struct HydrateState {
        std::vector<NewgroundsTrack> tracks;
        std::size_t remaining = 0;
        std::string listTitle;
        NewgroundsListCallback callback;
    };
    auto state = std::make_shared<HydrateState>();
    state->tracks.resize(seeds.size());
    state->remaining = seeds.size();
    state->listTitle = std::move(listTitle);
    state->callback = std::move(callback);

    auto finishOne = [state](std::size_t index, NewgroundsTrack track) {
        state->tracks[index] = std::move(track);
        if (--state->remaining > 0) return;

        NewgroundsListResult result;
        result.success = true;
        result.listTitle = state->listTitle;
        result.tracks = std::move(state->tracks);
        if (state->callback) state->callback(std::move(result));
    };

    for (std::size_t i = 0; i < seeds.size(); ++i) {
        auto const& seed = seeds[i];
        NewgroundsTrack fallback;
        fallback.songId = seed.songId;
        fallback.title = seed.title;

        if (auto found = infoCache().find(seed.songId); found != infoCache().end()) {
            auto track = found->second;
            if (track.title.empty()) track.title = seed.title;
            finishOne(i, std::move(track));
            continue;
        }

        fetchSongInfoUncached(seed.songId,
            [i, fallback, finishOne](NewgroundsSongResult result) mutable {
                auto track = result.success ? std::move(result.track) : fallback;
                if (track.title.empty()) track.title = fallback.title;
                finishOne(i, std::move(track));
            });
    }
}

std::vector<SeedTrack> parseWeeklyFeed(std::string const& xml) {
    std::vector<SeedTrack> seeds;
    std::unordered_set<int> seen;
    std::size_t pos = 0;
    while ((pos = xml.find("<item>", pos)) != std::string::npos) {
        auto end = xml.find("</item>", pos);
        if (end == std::string::npos) break;
        auto item = xml.substr(pos, end - pos);
        auto title = decodeHtml(between(item, "<title>", "</title>"));
        auto pageUrl = between(item, "<link>", "</link>");
        auto id = parseNewgroundsSongId(pageUrl);
        if (!title.empty() && id > 0 && seen.insert(id).second) {
            seeds.push_back({id, std::move(title)});
        }
        pos = end + 7;
    }
    return seeds;
}

std::vector<SeedTrack> parseNewgroundsSearchResults(std::string const& html) {
    std::vector<SeedTrack> seeds;
    std::unordered_set<int> seen;
    std::size_t pos = 0;

    while ((pos = html.find("class=\"item-audiosubmission", pos)) != std::string::npos) {
        auto anchorStart = html.rfind("<a ", pos);
        auto anchorEnd = html.find('>', pos);
        if (anchorStart == std::string::npos || anchorEnd == std::string::npos) {
            pos += 27;
            continue;
        }

        auto anchor = html.substr(anchorStart, anchorEnd - anchorStart + 1);
        auto href = decodeHtml(between(anchor, "href=\"", "\""));
        auto title = decodeHtml(between(anchor, "title=\"", "\""));
        auto id = parseNewgroundsSongId(href);

        if (id > 0 && !title.empty() && seen.insert(id).second) {
            seeds.push_back({id, std::move(title)});
            if (seeds.size() >= 15) break;
        }
        pos = anchorEnd + 1;
    }
    return seeds;
}

std::unordered_set<int>& downloadingSet() {
    static std::unordered_set<int> set;
    return set;
}

void finishDownload(int songId, NewgroundsDownloadCallback& callback, NewgroundsDownloadResult result) {
    downloadingSet().erase(songId);
    if (result.success) {
        MenuMusicLibrary::get().load();
        MenuMusicLibrary::get().syncDownloadedSongs();
    }
    if (callback) callback(std::move(result));
}

} // namespace

int parseNewgroundsSongId(std::string const& text) {
    auto trimmed = trim(text);
    if (trimmed.empty()) return 0;

    constexpr char const* listenMarker = "/audio/listen/";
    if (auto pos = trimmed.find(listenMarker); pos != std::string::npos) {
        return parseDigitsAt(trimmed, pos + 14);
    }
    if (auto pos = trimmed.find("audio.ngfiles.com/"); pos != std::string::npos) {
        auto slash = trimmed.find('/', pos + 18);
        if (slash == std::string::npos) return 0;
        return parseDigitsAt(trimmed, slash + 1);
    }
    if (std::all_of(trimmed.begin(), trimmed.end(),
        [](unsigned char c) { return std::isdigit(c) != 0; })) {
        return parseDigitsAt(trimmed, 0);
    }
    return 0;
}

void fetchWeeklyPicks(NewgroundsListCallback callback) {
    auto request = web::WebRequest();
    request.timeout(std::chrono::seconds(12));
    request.userAgent(kBrowserUserAgent);

    WebHelper::dispatch(std::move(request), "GET",
        "https://rss.ngfiles.com/weeklyaudiotop5.xml",
        [callback = std::move(callback)](web::WebResponse response) mutable {
            if (!response.ok()) {
                NewgroundsListResult result;
                result.error = "The Newgrounds weekly feed is unavailable right now.";
                result.listTitle = "Weekly Top 5";
                if (callback) callback(std::move(result));
                return;
            }
            hydrateSeeds(
                parseWeeklyFeed(response.string().unwrapOr("")),
                "Weekly Top 5",
                std::move(callback)
            );
        });
}

void searchNewgroundsSongs(std::string const& query, NewgroundsListCallback callback) {
    auto cleaned = trim(query);
    auto url = "https://www.newgrounds.com/search/summary?suitabilities=etm&terms=" +
        urlEncode(cleaned);

    auto request = web::WebRequest();
    request.timeout(std::chrono::seconds(15));
    request.userAgent(kBrowserUserAgent);
    request.header("Accept", "text/html,application/xhtml+xml");

    WebHelper::dispatch(std::move(request), "GET", url,
        [cleaned, callback = std::move(callback)](web::WebResponse response) mutable {
            auto listTitle = fmt::format("Results for \"{}\"", cleaned);
            if (!response.ok() || response.code() != 200) {
                NewgroundsListResult result;
                result.error =
                    "Newgrounds search is unavailable right now. Try again in a "
                    "minute, or paste the song ID / listen URL instead.";
                result.listTitle = std::move(listTitle);
                if (callback) callback(std::move(result));
                return;
            }

            auto seeds = parseNewgroundsSearchResults(response.string().unwrapOr(""));
            if (seeds.empty()) {
                NewgroundsListResult result;
                result.error = fmt::format(
                    "No Newgrounds songs matched \"{}\". If the song exists, "
                    "paste its ID or /audio/listen/ URL.", cleaned);
                result.listTitle = std::move(listTitle);
                if (callback) callback(std::move(result));
                return;
            }
            hydrateSeeds(std::move(seeds), std::move(listTitle), std::move(callback));
        });
}

void fetchNewgroundsSongInfo(int songId, NewgroundsSongCallback callback) {
    if (songId <= 0) {
        NewgroundsSongResult result;
        result.error = "Invalid song ID.";
        if (callback) callback(std::move(result));
        return;
    }

    if (auto found = infoCache().find(songId); found != infoCache().end()) {
        NewgroundsSongResult result;
        result.success = found->second.gdAvailable;
        result.track = found->second;
        if (!result.success) {
            result.error = "This song is not registered on GD's servers.";
        }
        if (callback) callback(std::move(result));
        return;
    }
    fetchSongInfoUncached(songId, std::move(callback));
}

void downloadNewgroundsSong(int songId, NewgroundsDownloadCallback callback) {
    auto* mdm = MusicDownloadManager::sharedState();
    if (!mdm || songId <= 0) {
        NewgroundsDownloadResult result;
        result.error = "The GD music downloader is unavailable.";
        if (callback) callback(std::move(result));
        return;
    }

    if (mdm->isSongDownloaded(songId)) {
        NewgroundsDownloadResult result;
        result.success = true;
        result.path = mdm->pathForSong(songId);
        finishDownload(songId, callback, std::move(result));
        return;
    }

    if (!downloadingSet().insert(songId).second) {
        NewgroundsDownloadResult result;
        result.error = "This song is already downloading.";
        if (callback) callback(std::move(result));
        return;
    }

    mdm->downloadSong(songId);

    // GD downloads in the background with no completion callback, so poll.
    auto attempts = std::make_shared<int>(0);
    auto sharedCallback = std::make_shared<NewgroundsDownloadCallback>(std::move(callback));
    auto poll = std::make_shared<geode::CopyableFunction<void()>>();
    *poll = [songId, attempts, sharedCallback, poll]() {
        auto* mdm = MusicDownloadManager::sharedState();
        if (!mdm) {
            NewgroundsDownloadResult result;
            result.error = "The GD music downloader is unavailable.";
            auto cb = std::move(*sharedCallback);
            *poll = {};
            finishDownload(songId, cb, std::move(result));
            return;
        }

        if (mdm->isSongDownloaded(songId)) {
            NewgroundsDownloadResult result;
            result.success = true;
            result.path = mdm->pathForSong(songId);
            auto cb = std::move(*sharedCallback);
            *poll = {};
            finishDownload(songId, cb, std::move(result));
            return;
        }

        if (++(*attempts) >= 90) {
            NewgroundsDownloadResult result;
            result.error =
                "The download did not complete. The song may not be "
                "downloadable from GD's servers.";
            auto cb = std::move(*sharedCallback);
            *poll = {};
            finishDownload(songId, cb, std::move(result));
            return;
        }

        paimon::scheduleMainThreadDelay(1.0f, [poll]() {
            if (*poll) (*poll)();
        });
    };
    paimon::scheduleMainThreadDelay(1.0f, [poll]() {
        if (*poll) (*poll)();
    });
}

bool isNewgroundsSongDownloading(int songId) {
    return downloadingSet().contains(songId);
}

bool isNewgroundsSongDownloaded(int songId) {
    auto* mdm = MusicDownloadManager::sharedState();
    return mdm && songId > 0 && mdm->isSongDownloaded(songId);
}

std::string newgroundsSongLocalPath(int songId) {
    auto* mdm = MusicDownloadManager::sharedState();
    if (!mdm || songId <= 0) return {};
    return mdm->pathForSong(songId);
}

} // namespace paimon::menumusic
