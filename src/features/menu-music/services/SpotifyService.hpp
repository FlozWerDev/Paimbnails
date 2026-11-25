#pragma once
#include <Geode/Geode.hpp>
#include <string>
#include <functional>

namespace paimon::menumusic {

class SpotifyService {
public:
    static SpotifyService& get();

    // account authentication and OAuth profile credentials
    bool isLinked() const;
    std::string getUserName() const;
    void linkAccount(std::string const& username, std::string const& token = "");
    void unlinkAccount();

    // auto-sync setting (pause on PlayLayer, resume on Menu)
    bool isAutoSyncEnabled() const;
    void setAutoSyncEnabled(bool enabled);

    // URI scheme deep links and browser authentication
    void openSpotifyAppOrWeb() const;
    void openAuthPortal() const;
    void openTrack(std::string const& spotifyUrl) const;

    // track metadata & banner fetcher via Spotify official OEmbed API (100% legal, no auth required)
    void fetchSpotifyTrack(
        std::string const& url,
        std::function<void(bool ok, std::string title, std::string artist, std::string coverPath)> callback
    );

    // game lifecycle hooks (auto-pause playback on PlayLayer, resume on MenuLayer)
    void onEnterMenu();
    void onEnterLevel();

    // commands target Spotify's desktop media session only (Windows).
    void sendMediaCommand(std::string const& command); // "play_pause", "next", "prev"

    static std::string extractTrackId(std::string const& url);

private:
    SpotifyService() = default;
};

} // namespace paimon::menumusic
