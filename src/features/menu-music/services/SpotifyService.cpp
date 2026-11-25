#include "../../../utils/FilesystemPath.hpp"
#include "SpotifyService.hpp"
#include "SpotifyMediaControl.hpp"
#include "MenuMusicLibrary.hpp"
#include "../../../utils/HttpClient.hpp"
#include "../../../utils/WebHelper.hpp"
#include <Geode/utils/web.hpp>
#include <Geode/utils/file.hpp>
#include <Geode/utils/string.hpp>
#include <matjson.hpp>
#include <chrono>

namespace paimon::menumusic {

SpotifyService& SpotifyService::get() {
    static SpotifyService instance;
    return instance;
}

bool SpotifyService::isLinked() const {
    return geode::Mod::get()->getSavedValue<bool>("spotify-account-linked", false);
}

std::string SpotifyService::getUserName() const {
    return geode::Mod::get()->getSavedValue<std::string>("spotify-username", "Usuario Spotify");
}

void SpotifyService::linkAccount(std::string const& username, std::string const& token) {
    auto* mod = geode::Mod::get();
    mod->setSavedValue("spotify-account-linked", true);
    mod->setSavedValue("spotify-username", username.empty() ? "Usuario Spotify" : username);
    if (!token.empty()) {
        mod->setSavedValue("spotify-access-token", token);
    }
}

void SpotifyService::unlinkAccount() {
    auto* mod = geode::Mod::get();
    mod->setSavedValue("spotify-account-linked", false);
    mod->setSavedValue("spotify-username", std::string());
    mod->setSavedValue("spotify-access-token", std::string());
}

bool SpotifyService::isAutoSyncEnabled() const {
    return geode::Mod::get()->getSavedValue<bool>("spotify-autosync", true);
}

void SpotifyService::setAutoSyncEnabled(bool enabled) {
    geode::Mod::get()->setSavedValue("spotify-autosync", enabled);
}

void SpotifyService::openSpotifyAppOrWeb() const {
    geode::utils::web::openLinkInBrowser("https://open.spotify.com");
}

void SpotifyService::openAuthPortal() const {
    geode::utils::web::openLinkInBrowser("https://accounts.spotify.com");
}

std::string SpotifyService::extractTrackId(std::string const& url) {
    size_t pos = url.find("track/");
    if (pos == std::string::npos) pos = url.find("track%2F");
    if (pos != std::string::npos) {
        size_t start = pos + (url.find("track/") != std::string::npos ? 6 : 8);
        size_t end = url.find_first_of("?&/ ", start);
        return url.substr(start, end == std::string::npos ? std::string::npos : end - start);
    }
    return std::to_string(std::hash<std::string>{}(url));
}

void SpotifyService::openTrack(std::string const& spotifyUrl) const {
    std::string trackId = extractTrackId(spotifyUrl);
    if (!trackId.empty() && trackId.find_first_not_of("0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ") == std::string::npos) {
        std::string uri = "spotify:track:" + trackId;
        geode::utils::web::openLinkInBrowser(uri);
    } else {
        openSpotifyAppOrWeb();
    }
}

void SpotifyService::fetchSpotifyTrack(
    std::string const& url,
    std::function<void(bool ok, std::string title, std::string artist, std::string coverPath)> callback
) {
    std::string trackId = extractTrackId(url);
    std::string oembedUrl = "https://open.spotify.com/oembed?url=" + HttpClient::encodeQueryParam(url);

    auto req = geode::utils::web::WebRequest();
    req.header("User-Agent", "Paimbnails-MenuMusic/1.0");

    WebHelper::dispatch(std::move(req), "GET", oembedUrl, [url, trackId, callback](geode::utils::web::WebResponse res) {
        if (!res.ok()) {
            if (callback) callback(false, "", "", "");
            return;
        }

        auto jsonStr = res.string().unwrapOr("");
        auto parsed = matjson::parse(jsonStr);
        if (!parsed.isOk() || !parsed.unwrap().isObject()) {
            if (callback) callback(false, "", "", "");
            return;
        }

        auto const& obj = parsed.unwrap();
        std::string title = obj.contains("title") ? obj["title"].asString().unwrapOr("Spotify Track") : "Spotify Track";
        std::string artist = obj.contains("author_name") ? obj["author_name"].asString().unwrapOr("Spotify") : "Spotify";
        std::string thumbnailUrl = obj.contains("thumbnail_url") ? obj["thumbnail_url"].asString().unwrapOr("") : "";

        if (thumbnailUrl.empty()) {
            MusicTrack t;
            t.id = "spotify_" + trackId;
            t.displayName = title;
            t.artist = artist;
            t.sourceUrl = url;
            t.source = TrackSource::Downloaded;
            t.addedUnixMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            MenuMusicLibrary::get().addTrack(t);
            if (callback) callback(true, title, artist, "");
            return;
        }

        auto imgReq = geode::utils::web::WebRequest();
        imgReq.header("User-Agent", "Paimbnails-MenuMusic/1.0");

        WebHelper::dispatch(std::move(imgReq), "GET", thumbnailUrl, [url, trackId, title, artist, callback](geode::utils::web::WebResponse imgRes) {
            if (!imgRes.ok()) {
                if (callback) callback(true, title, artist, "");
                return;
            }

            auto data = imgRes.data();
            if (data.empty()) {
                if (callback) callback(true, title, artist, "");
                return;
            }

            auto coversDir = paimon::paths::toStdPath(geode::Mod::get()->getSaveDir()) / "covers";
            std::error_code ec;
            std::filesystem::create_directories(coversDir, ec);

            auto destCover = coversDir / ("spotify_" + trackId + ".jpg");
            bool wrote = geode::utils::file::writeBinary(paimon::paths::toSdkPath(destCover), data).isOk();

            std::string coverPathStr = wrote ? paimon::paths::toUtf8(destCover) : "";

            MusicTrack t;
            t.id = "spotify_" + trackId;
            t.displayName = title;
            t.artist = artist;
            t.coverPath = coverPathStr;
            t.sourceUrl = url;
            t.source = TrackSource::Downloaded;
            t.addedUnixMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            MenuMusicLibrary::get().addTrack(t);

            if (callback) {
                callback(true, title, artist, coverPathStr);
            }
        });
    });
}

void SpotifyService::sendMediaCommand(std::string const& command) {
    sendSpotifyMediaCommand(command);
}

void SpotifyService::onEnterMenu() {
    if (isLinked() && isAutoSyncEnabled()) {
        sendMediaCommand("play");
    }
}

void SpotifyService::onEnterLevel() {
    if (isLinked() && isAutoSyncEnabled()) {
        sendMediaCommand("pause");
    }
}

} // namespace paimon::menumusic
