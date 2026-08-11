#pragma once

// Newgrounds discovery uses search/RSS results; song info, streams, and
// downloads go through GD's infrastructure to avoid anti-bot blocks.

#include <functional>
#include <string>
#include <vector>

namespace paimon::menumusic {

struct NewgroundsTrack {
    int songId = 0;
    std::string title;
    std::string artist;
    float sizeMb = 0.f;       // 0 = unknown.
    std::string streamUrl;    // Decoded URL; empty if unavailable.
    bool gdAvailable = false; // Known to GD for info/download.
};

struct NewgroundsListResult {
    bool success = false;
    std::string error;
    std::string listTitle;    // Display title.
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

// Weekly Audio Top 5 (RSS), hydrated through GD.
void fetchWeeklyPicks(NewgroundsListCallback callback);

// Search by song name/artist.
void searchNewgroundsSongs(std::string const& query, NewgroundsListCallback callback);

// Session-cached song info by ID.
void fetchNewgroundsSongInfo(int songId, NewgroundsSongCallback callback);

// Download through MusicDownloadManager, register in the library, and continue
// after the calling popup closes.
void downloadNewgroundsSong(int songId, NewgroundsDownloadCallback callback);
bool isNewgroundsSongDownloading(int songId);
bool isNewgroundsSongDownloaded(int songId);
std::string newgroundsSongLocalPath(int songId);

// Extract a song ID from a number or Newgrounds audio URL; return 0 if absent.
int parseNewgroundsSongId(std::string const& text);

}
