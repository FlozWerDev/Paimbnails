#pragma once

// newgrounds discovery uses search/rss results; song info, streams, and
// downloads go through gd's infrastructure to avoid anti-bot blocks.

#include <functional>
#include <string>
#include <vector>

namespace paimon::menumusic {

struct NewgroundsTrack {
    int songId = 0;
    std::string title;
    std::string artist;
    float sizeMb = 0.f;       // 0 = unknown.
    std::string streamUrl;    // decoded url; empty if unavailable.
    bool gdAvailable = false; // known to gd for info/download.
};

struct NewgroundsListResult {
    bool success = false;
    std::string error;
    std::string listTitle;    // display title.
    std::vector<NewgroundsTrack> tracks;
};

struct NewgroundsSongResult {
    bool success = false;
    std::string error;
    NewgroundsTrack track;
};

struct NewgroundsDownloadResult {
    bool success = false;
    std::string error;
    std::string path;
};

using NewgroundsListCallback = std::function<void(NewgroundsListResult)>;
using NewgroundsSongCallback = std::function<void(NewgroundsSongResult)>;
using NewgroundsDownloadCallback = std::function<void(NewgroundsDownloadResult)>;

// weekly audio top 5 (rss), hydrated through gd.
void fetchWeeklyPicks(NewgroundsListCallback callback);

// search by song name/artist.
void searchNewgroundsSongs(std::string const& query, NewgroundsListCallback callback);

// session-cached song info by id.
void fetchNewgroundsSongInfo(int songId, NewgroundsSongCallback callback);

// download through musicdownloadmanager, register in the library, and continue
// after the calling popup closes.
void downloadNewgroundsSong(int songId, NewgroundsDownloadCallback callback);
bool isNewgroundsSongDownloading(int songId);
bool isNewgroundsSongDownloaded(int songId);
std::string newgroundsSongLocalPath(int songId);

// extract a song id from a number or newgrounds audio url; return 0 if absent.
int parseNewgroundsSongId(std::string const& text);

}
