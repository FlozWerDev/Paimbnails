#include "../src/features/menu-loop/services/MenuLoopScan.hpp"
#include <cassert>
#include <chrono>
#include <iostream>

using namespace paimon::menuloop;
namespace fs = std::filesystem;

int main(int argc, char** argv) {
    assert(argc == 2); // Runner provides a fresh temporary directory.
    fs::path root = argv[1];
    fs::create_directories(root / "nested");
    auto write = [](fs::path const& path, std::string const& value = "audio") {
        std::ofstream stream(path);
        stream << value;
        assert(stream.good());
    };
    auto first = menuLoopPathString(root / "one.MP3");
    auto second = menuLoopPathString(root / "nested" / "two.ogg");
    auto blocked = menuLoopPathString(root / "blocked.wav");
    write(menuLoopUtf8Path(first));
    write(menuLoopUtf8Path(second));
    write(menuLoopUtf8Path(blocked));
    write(root / "ignore.png");
    write(root / "blacklist.txt", blocked + "\r\n" + blocked + "\n");
    write(root / "favorites.txt", " # comment\n  " + second + " \r\n" + second + "\n");
    // An audio-looking directory and a symlink loop must not become songs.
    fs::create_directory(root / "directory.mp3");
    std::error_code ec;
    fs::create_directory_symlink(root, root / "nested" / "loop", ec);

    MenuLoopScanInput input;
    input.configDir = input.extraFolder = root;
    input.restoreSaved = true;
    input.savedSong = second;
    auto scan = [&] { return scanMenuLoopSongs(input, [] { return false; }); };
    auto result = scan();
    assert(result.songs.size() == 2);
    assert(result.blacklist == std::vector<std::string>{blocked});
    assert(result.favorites == std::vector<std::string>{second});
    assert(result.selectedSong == second);

    input.savedSong = "missing";
    input.savedPath = first;
    assert(scan().selectedSong == first);
    input.savedPath = blocked;
    assert(scan().selectedSong == "menuLoop.mp3");
    input.restoreSaved = false;
    for (int i = 0; i < 20; ++i) {
        auto selected = scan().selectedSong;
        assert(selected == first || selected == second);
    }

    input.usePlaylist = true;
    write(root / "playlistOne.txt", " # list\r\n" + second + "\r\n" + first + "\n"
        + second + "\n" + blocked + "\n" + menuLoopPathString(root / "missing.mp3") + "\n");
    result = scan();
    assert((result.songs == std::vector<std::string>{second, first}));
    input.playlistFile = root / "missing-playlist.txt";
    assert(scan().songs.empty());
    input.playlistFile = root / "custom.txt";
    write(input.playlistFile, first + "\n");
    assert(scan().songs == std::vector<std::string>{first});

    input.usePlaylist = false;
    assert(scanMenuLoopSongs(input, [] { return true; }).songs.empty());
    int polls = 0;
    scanMenuLoopSongs(input, [&] { return ++polls > 5; });
    assert(polls < 20);
    input.configDir = input.extraFolder = root / "missing-folder";
    assert(scan().songs.empty());

    // Large playlists exercise deduplication/filtering without quadratic scans.
    input.configDir = root;
    input.usePlaylist = true;
    std::string playlist;
    for (int i = 0; i < 2000; ++i) playlist += first + "\n" + second + "\n";
    write(input.playlistFile, playlist);
    auto start = std::chrono::steady_clock::now();
    assert(scan().songs.size() == 2);
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
    std::cout << "Menu Loop scan regression passed (4000 playlist entries: " << elapsed << " ms)\n";
}
