#pragma once

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <unordered_set>
#include <vector>

namespace paimon::menuloop {

// Disk-only input/output: no Mod settings or live manager access on the worker.
struct MenuLoopScanInput {
    std::filesystem::path configDir, extraFolder, playlistFile;
    bool usePlaylist = false;
    bool restoreSaved = false;
    std::string savedSong, savedPath;
};

struct MenuLoopScanResult {
    std::vector<std::string> songs, blacklist, favorites;
    std::string selectedSong = "menuLoop.mp3";
};

inline std::string menuLoopPathString(std::filesystem::path const& path) {
    auto value = path.u8string();
    return {value.begin(), value.end()};
}

inline std::filesystem::path menuLoopUtf8Path(std::string const& value) {
    return std::filesystem::path(std::u8string(value.begin(), value.end()));
}

template <class Cancelled, class Consume>
void readMenuLoopList(std::filesystem::path const& path, Cancelled cancelled, Consume consume) {
    std::ifstream stream(path);
    std::string line;
    while (!cancelled() && std::getline(stream, line)) {
        auto first = line.find_first_not_of(" \t\r\n");
        if (first == std::string::npos || line[first] == '#') continue;
        auto last = line.find_last_not_of(" \t\r\n");
        consume(line.substr(first, last - first + 1));
    }
}

template <class Cancelled>
MenuLoopScanResult scanMenuLoopSongs(MenuLoopScanInput const& input, Cancelled cancelled) {
    MenuLoopScanResult result;
    std::unordered_set<std::string> seen, blocked, favorites;
    auto readLines = [&](std::filesystem::path const& path, auto consume) {
        readMenuLoopList(path, cancelled, consume);
    };
    readLines(input.configDir / "blacklist.txt", [&](std::string line) {
        if (blocked.insert(line).second) result.blacklist.push_back(std::move(line));
    });
    readLines(input.configDir / "favorites.txt", [&](std::string line) {
        if (!blocked.contains(line) && favorites.insert(line).second)
            result.favorites.push_back(std::move(line));
    });
    auto add = [&](std::string song) {
        if (!blocked.contains(song) && seen.insert(song).second)
            result.songs.push_back(std::move(song));
    };

    if (input.usePlaylist) {
        auto path = input.playlistFile.empty()
            ? input.configDir / "playlistOne.txt" : input.playlistFile;
        readLines(path, [&](std::string line) {
            if (blocked.contains(line) || seen.contains(line)) return;
            std::error_code ec;
            if (std::filesystem::is_regular_file(menuLoopUtf8Path(line), ec)) add(std::move(line));
        });
    } else {
        auto scan = [&](std::filesystem::path const& dir) {
            if (dir.empty()) return;
            std::error_code ec;
            std::filesystem::recursive_directory_iterator it(
                dir, std::filesystem::directory_options::skip_permission_denied, ec), end;
            while (!ec && it != end && !cancelled()) {
                auto ext = menuLoopPathString(it->path().extension());
                std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
                    return static_cast<char>(std::tolower(c));
                });
                // Reject non-audio entries before querying their file status.
                if (ext == ".mp3" || ext == ".ogg" || ext == ".wav" || ext == ".flac"
                    || ext == ".oga" || ext == ".m4a" || ext == ".opus") {
                    std::error_code statusError;
                    if (it->is_regular_file(statusError)) add(menuLoopPathString(it->path()));
                }
                it.increment(ec); // Nonthrowing if a folder disappears mid-scan.
            }
        };
        scan(input.configDir);
        if (input.extraFolder.lexically_normal() != input.configDir.lexically_normal())
            scan(input.extraFolder);
    }

    if (cancelled()) return result;
    if (input.restoreSaved) {
        if (seen.contains(input.savedSong) && !blocked.contains(input.savedSong))
            result.selectedSong = input.savedSong;
        else if (seen.contains(input.savedPath) && !blocked.contains(input.savedPath))
            result.selectedSong = input.savedPath;
    } else if (!result.songs.empty()) {
        // Favorites retain their double weight. No second filesystem pass.
        std::vector<size_t> candidates;
        candidates.reserve(result.songs.size() * 2);
        for (size_t i = 0; i < result.songs.size(); ++i) {
            candidates.push_back(i);
            if (favorites.contains(result.songs[i])) candidates.push_back(i);
        }
        std::mt19937 random(std::random_device{}());
        result.selectedSong = result.songs[candidates[
            std::uniform_int_distribution<size_t>(0, candidates.size() - 1)(random)]];
    }
    return result;
}

} // namespace paimon::menuloop
