#include "../../../utils/FilesystemPath.hpp"
#include "MenuMusicLibrary.hpp"
#include "FfmpegBootstrap.hpp"
#include "YtDlpDownloader.hpp"

#include <Geode/binding/MusicDownloadManager.hpp>
#include <Geode/binding/SongInfoObject.hpp>
#include <Geode/loader/Mod.hpp>
#include "../../../utils/JsonHelper.hpp"
#include "../../../utils/AtomicFileWrite.hpp"
#include "../../../utils/ThreadTracker.hpp"
#include "../../../utils/LocalAssetStore.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include <Geode/utils/file.hpp>
#include <Geode/utils/cocos.hpp>
#include <Geode/utils/string.hpp>
#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <climits>
#include <deque>
#include <fmt/format.h>
#include <unordered_set>

using namespace geode::prelude;

namespace paimon::menumusic {

struct DownloadedSongSnapshot {
    cocos2d::CCDictionary* dictionary = nullptr; // identity only; look up the live dictionary each frame
    std::size_t count = 0;
    std::uint64_t generation = 0;
    std::string cursor;
    std::deque<MusicTrack> candidates;
};

namespace {

constexpr std::uintmax_t kMaxLibraryFileBytes = 16ull * 1024 * 1024;

// read GD objects only on the main thread; workers receive independent values.
std::vector<MusicTrack> downloadedSongCandidates(MusicDownloadManager* manager, CCDictionary* songs) {
    std::vector<MusicTrack> candidates;
    candidates.reserve(songs->count());
    auto const now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    for (auto [key, song] : CCDictionaryExt<std::string_view, SongInfoObject*>(songs)) {
        if (!song || manager->isResourceSong(song->m_songID)) continue;
        auto path = manager->pathForSong(song->m_songID);
        if (!MenuMusicLibrary::isAudioExtension(path)) continue;
        MusicTrack track;
        track.id = fmt::format("gd_{}", song->m_songID);
        track.audioPath = std::move(path);
        track.displayName = song->m_songName;
        track.artist = song->m_artistName;
        track.source = TrackSource::GeometryDash;
        track.addedUnixMs = now;
        candidates.push_back(std::move(track));
    }
    return candidates;
}

void filterMissingDownloadedSongs(std::vector<MusicTrack>& candidates) {
    std::erase_if(candidates, [](MusicTrack const& track) {
        std::error_code ec;
        return !std::filesystem::is_regular_file(paimon::assets::pathFromUtf8(track.audioPath), ec) || ec;
    });
}

} // namespace

MenuMusicLibrary& MenuMusicLibrary::get() {
    static MenuMusicLibrary instance;
    return instance;
}

MenuMusicLibrary::MenuMusicLibrary() {
    ensureDirs();
}

void MenuMusicLibrary::ensureDirs() {
    std::error_code ec;
    std::filesystem::create_directories(getTracksDir(), ec);
    std::filesystem::create_directories(getCoversDir(), ec);
}

void MenuMusicLibrary::repairDownloadedCoversAsync() {
    struct CoverRepair {
        std::string trackId;
        std::string originalPath;
        std::string displayPath;
    };
    std::vector<CoverRepair> covers;
    for (auto const& track : m_tracks) {
        if (track.source != TrackSource::Downloaded || track.coverPath.empty()) continue;
        auto const extension = geode::utils::string::toLower(paimon::paths::toUtf8(
            paimon::assets::pathFromUtf8(track.coverPath).extension()));
        if (extension == ".webp") covers.push_back({track.id, track.coverPath, {}});
    }
    if (covers.empty()) return;

    auto ffmpegPath = FfmpegBootstrap::get().bundledPath();
    if (!paimon::ThreadTracker::get().spawn(
            [this, covers = std::move(covers), ffmpegPath]() mutable {
        for (auto& cover : covers) {
            if (paimon::isRuntimeShuttingDown()) return;
            cover.displayPath = paimon::paths::toUtf8(YtDlpDownloader::prepareCoverForDisplay(
                paimon::assets::pathFromUtf8(cover.originalPath), ffmpegPath));
        }
        if (paimon::isRuntimeShuttingDown()) return;
        Loader::get()->queueInMainThread([this, covers = std::move(covers)] {
            if (paimon::isRuntimeShuttingDown()) return;
            bool changed = false;
            for (auto const& cover : covers) {
                auto* track = findTrack(cover.trackId);
                if (!track || track->source != TrackSource::Downloaded ||
                    track->coverPath != cover.originalPath || cover.displayPath == cover.originalPath) continue;
                track->coverPath = cover.displayPath;
                changed = true;
            }
            if (changed) {
                markDirty();
                notifyChanged();
            }
        });
    })) {
        log::warn("[MenuMusic] could not schedule downloaded cover repair");
    }
}

std::filesystem::path MenuMusicLibrary::getRootDir() const {
    return paimon::paths::toStdPath(Mod::get()->getSaveDir()) / "menu-music";
}
std::filesystem::path MenuMusicLibrary::getTracksDir() const {
    return getRootDir() / "tracks";
}
std::filesystem::path MenuMusicLibrary::getCoversDir() const {
    return getRootDir() / "covers";
}
std::filesystem::path MenuMusicLibrary::getLibraryFile() const {
    return getRootDir() / "library.json";
}


void MenuMusicLibrary::rebuildTrackIndex() {
    m_trackIndex.clear();
    m_trackIndex.reserve(m_tracks.size());
    for (size_t i = 0; i < m_tracks.size(); ++i) {
        m_trackIndex[m_tracks[i].id] = i;
    }
}

void MenuMusicLibrary::markDirty() {
    if (m_savePending) return;
    m_savePending = true;
    Loader::get()->queueInMainThread([this] {
        if (m_savePending) {
            m_savePending = false;
            save();
        }
    });
}


MusicTrack* MenuMusicLibrary::findTrack(const std::string& id) {
    return const_cast<MusicTrack*>(static_cast<MenuMusicLibrary const*>(this)->findTrack(id));
}

const MusicTrack* MenuMusicLibrary::findTrack(const std::string& id) const {
    auto it = m_trackIndex.find(id);
    if (it != m_trackIndex.end() && it->second < m_tracks.size()) {
        return &m_tracks[it->second];
    }
    return nullptr;
}

MusicTrack* MenuMusicLibrary::findTrackByAudioPath(const std::string& path) {
    return const_cast<MusicTrack*>(static_cast<MenuMusicLibrary const*>(this)->findTrackByAudioPath(path));
}

const MusicTrack* MenuMusicLibrary::findTrackByAudioPath(const std::string& path) const {
    auto it = std::find_if(m_tracks.begin(), m_tracks.end(), [&](const MusicTrack& track) {
        return track.audioPath == path;
    });
    return it == m_tracks.end() ? nullptr : &*it;
}

void MenuMusicLibrary::addTrack(const MusicTrack& track) {
    if (auto* existing = findTrack(track.id)) {
        bool favorite = existing->favorite;
        bool blacklisted = existing->blacklisted;
        *existing = track;
        existing->favorite = favorite;
        existing->blacklisted = blacklisted;
        markDirty();
        notifyChanged();
        return;
    }
    if (auto* existing = findTrackByAudioPath(track.audioPath)) {
        if (existing->displayName.empty()) existing->displayName = track.displayName;
        if (existing->artist.empty()) existing->artist = track.artist;
        if (existing->coverPath.empty()) existing->coverPath = track.coverPath;
        markDirty();
        notifyChanged();
        return;
    }
    m_trackIndex[track.id] = m_tracks.size();
    m_tracks.push_back(track);
    markDirty();
    notifyChanged();
}

void MenuMusicLibrary::setFavorite(const std::string& id, bool favorite) {
    auto* track = findTrack(id);
    if (!track || (track->favorite == favorite && (!favorite || !track->blacklisted))) return;
    track->favorite = favorite;
    if (favorite) track->blacklisted = false;
    markDirty();
    notifyChanged();
}

void MenuMusicLibrary::setBlacklisted(const std::string& id, bool blacklisted) {
    auto* track = findTrack(id);
    if (!track || (track->blacklisted == blacklisted && (!blacklisted || !track->favorite))) return;
    track->blacklisted = blacklisted;
    if (blacklisted) track->favorite = false;
    markDirty();
    notifyChanged();
}

std::size_t MenuMusicLibrary::importFolder(const std::filesystem::path& folder, bool recursive) {
    std::error_code ec;
    if (!std::filesystem::is_directory(folder, ec) || ec) return 0;

    std::vector<std::filesystem::path> files;
    auto collect = [&](const auto& entry) {
        std::error_code typeEc;
        if (entry.is_regular_file(typeEc) && !typeEc && isAudioExtension(entry.path())) {
            files.push_back(entry.path());
        }
    };
    auto collectEntries = [&](auto first, auto last) {
        for (auto it = first; it != last; ++it) {
            if (ec) {
                ec.clear();
                continue;
            }
            collect(*it);
        }
    };

    if (recursive) {
        collectEntries(
            std::filesystem::recursive_directory_iterator(
                folder, std::filesystem::directory_options::skip_permission_denied, ec),
            std::filesystem::recursive_directory_iterator{});
    } else {
        collectEntries(
            std::filesystem::directory_iterator(
                folder, std::filesystem::directory_options::skip_permission_denied, ec),
            std::filesystem::directory_iterator{});
    }

    const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    std::size_t added = 0;
    // local path set: same equality as findTrackByAudioPath, O(1) per file
    // instead of a linear scan, with no persistent index to keep in sync.
    std::unordered_set<std::string> knownPaths;
    knownPaths.reserve(m_tracks.size());
    for (auto const& t : m_tracks) knownPaths.insert(t.audioPath);
    for (const auto& path : files) {
        auto normalized = paimon::paths::toUtf8(path);
        if (!knownPaths.insert(normalized).second) continue;

        MusicTrack track;
        track.id = generateId("folder");
        track.audioPath = normalized;
        track.displayName = paimon::paths::toUtf8(path.stem());
        track.source = TrackSource::Local;
        track.addedUnixMs = now;
        m_trackIndex[track.id] = m_tracks.size();
        m_tracks.push_back(std::move(track));
        knownPaths.insert(m_tracks.back().audioPath);
        ++added;
    }

    if (added) {
        markDirty();
        notifyChanged();
    }
    return added;
}

std::size_t MenuMusicLibrary::syncDownloadedSongs(bool force) {
    auto* manager = MusicDownloadManager::sharedState();
    auto* songs = manager ? manager->m_songObjects : nullptr;
    if (!manager || !songs) return 0;

    // getDownloadedSongs() calls isSongDownloaded() for every known song,
    // including filesystem checks. Read metadata directly; validate files separately.
    const auto songCount = static_cast<std::size_t>(songs->count());
    if (!force && m_syncedSongDictionary == songs && m_syncedSongCount == songCount) return 0;
    m_syncedSongCount = songCount;
    m_syncedSongDictionary = songs;

    auto candidates = downloadedSongCandidates(manager, songs);
    filterMissingDownloadedSongs(candidates);
    return mergeDownloadedSongs(std::move(candidates));
}

void MenuMusicLibrary::syncDownloadedSongsAsync() {
    auto* manager = MusicDownloadManager::sharedState();
    auto* songs = manager ? manager->m_songObjects : nullptr;
    if (!songs || m_downloadSyncPending || paimon::isRuntimeShuttingDown()) return;
    auto const songCount = static_cast<std::size_t>(songs->count());
    if (m_syncedSongDictionary == songs && m_syncedSongCount == songCount) return;

    auto snapshot = std::make_shared<DownloadedSongSnapshot>();
    snapshot->dictionary = songs;
    snapshot->count = songCount;
    snapshot->generation = m_downloadSyncGeneration;
    m_downloadSyncPending = true;
    // no enumeration or path resolution during popup construction. Geode swaps
    // its main-thread queue each frame, so every continuation runs later.
    Loader::get()->queueInMainThread([this, snapshot] { captureDownloadedSongBatch(snapshot); });
}

void MenuMusicLibrary::restartDownloadedSongSync() {
    m_downloadSyncPending = false;
    syncDownloadedSongsAsync();
    // a pending Play request must also finish when there is no work to start.
    if (!m_downloadSyncPending && !paimon::isRuntimeShuttingDown()) notifyChanged();
}

void MenuMusicLibrary::captureDownloadedSongBatch(std::shared_ptr<DownloadedSongSnapshot> const& snapshot) {
    if (paimon::isRuntimeShuttingDown()) { m_downloadSyncPending = false; return; }
    auto* manager = MusicDownloadManager::sharedState();
    auto* songs = manager ? manager->m_songObjects : nullptr;
    if (!songs) { m_downloadSyncPending = false; notifyChanged(); return; }
    if (songs != snapshot->dictionary || songs->count() != snapshot->count ||
        snapshot->generation != m_downloadSyncGeneration) {
        restartDownloadedSongSync();
        return;
    }

    // count the cursor lookup against the same budget as metadata capture.
    constexpr std::size_t kMaxSongsPerFrame = 64;
    auto const deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(1);
    cocos2d::CCDictElement* next = songs->m_pElements;
    if (!snapshot->cursor.empty()) {
        cocos2d::CCDictElement* previous = nullptr;
        HASH_FIND_STR(songs->m_pElements, snapshot->cursor.c_str(), previous);
        if (!previous) { restartDownloadedSongSync(); return; }
        next = static_cast<cocos2d::CCDictElement*>(previous->hh.next);
    }
    auto const now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    std::size_t processed = 0;
    while (next && processed < kMaxSongsPerFrame &&
           (processed == 0 || std::chrono::steady_clock::now() < deadline)) {
        // a key is stable across frames; CCDictElement iterators are not.
        snapshot->cursor = next->getStrKey();
        auto* song = static_cast<SongInfoObject*>(next->getObject());
        if (song && !manager->isResourceSong(song->m_songID)) {
            MusicTrack track;
            track.id = fmt::format("gd_{}", song->m_songID);
            track.displayName = song->m_songName;
            track.artist = song->m_artistName;
            track.source = TrackSource::GeometryDash;
            track.addedUnixMs = now;
            track.audioPath = manager->pathForSong(song->m_songID);
            if (MenuMusicLibrary::isAudioExtension(track.audioPath)) snapshot->candidates.push_back(std::move(track));
        }
        ++processed;
        // re-find before following `next`: no native dictionary element survives
        // a yield or reentrant mutation. Hash lookup is O(1) on average.
        if (manager->m_songObjects != songs || songs->count() != snapshot->count ||
            snapshot->generation != m_downloadSyncGeneration) {
            restartDownloadedSongSync();
            return;
        }
        cocos2d::CCDictElement* current = nullptr;
        HASH_FIND_STR(songs->m_pElements, snapshot->cursor.c_str(), current);
        if (!current) { restartDownloadedSongSync(); return; }
        next = static_cast<cocos2d::CCDictElement*>(current->hh.next);
    }
    if (next) {
        Loader::get()->queueInMainThread([this, snapshot] { captureDownloadedSongBatch(snapshot); });
        return;
    }
    finishDownloadedSongCapture(snapshot);
}

void MenuMusicLibrary::finishDownloadedSongCapture(std::shared_ptr<DownloadedSongSnapshot> const& snapshot) {
    auto candidates = std::move(snapshot->candidates);
    auto const songCount = snapshot->count;
    auto const generation = snapshot->generation;
    auto* dictionary = snapshot->dictionary;
    bool started = false;
    try {
        started = paimon::ThreadTracker::get().spawn(
        [this, songCount, generation, dictionary, captured = std::move(candidates)]() mutable {
            if (paimon::isRuntimeShuttingDown()) return;
            std::vector<MusicTrack> candidates;
            candidates.reserve(captured.size());
            for (auto& track : captured) candidates.push_back(std::move(track));
            filterMissingDownloadedSongs(candidates);
            Loader::get()->queueInMainThread([this, songCount, generation, dictionary, candidates = std::move(candidates)]() mutable {
                if (paimon::isRuntimeShuttingDown()) return;
                auto* manager = MusicDownloadManager::sharedState();
                auto* songs = manager ? manager->m_songObjects : nullptr;
                if (!songs) { m_downloadSyncPending = false; notifyChanged(); return; }
                if (songs != dictionary || songs->count() != songCount || generation != m_downloadSyncGeneration) {
                    restartDownloadedSongSync();
                    return;
                }
                m_downloadSyncPending = false;
                // a manual sync may have run meanwhile. Merge against the current
                // library so user edits survive and no duplicate tracks are added.
                m_syncedSongCount = songCount;
                m_syncedSongDictionary = songs;
                mergeDownloadedSongs(std::move(candidates), true);
            });
        });
    } catch (std::exception const& error) {
        log::warn("[MenuMusic] could not start downloaded song scan: {}", error.what());
    }
    if (!started) { m_downloadSyncPending = false; notifyChanged(); }
}

std::size_t MenuMusicLibrary::mergeDownloadedSongs(std::vector<MusicTrack> candidates, bool notifyWhenUnchanged) {
    std::unordered_map<std::string, std::size_t> byPath;
    byPath.reserve(m_tracks.size() + candidates.size());
    for (std::size_t index = 0; index < m_tracks.size(); ++index)
        byPath.emplace(m_tracks[index].audioPath, index);
    std::size_t added = 0;
    bool changed = false;
    for (auto& track : candidates) {
        if (auto const found = byPath.find(track.audioPath); found != byPath.end()) {
            auto& existing = m_tracks[found->second];
            if (existing.displayName.empty() && !track.displayName.empty()) {
                existing.displayName = track.displayName;
                changed = true;
            }
            if (existing.artist.empty() && !track.artist.empty()) {
                existing.artist = track.artist;
                changed = true;
            }
            continue;
        }

        if (findTrack(track.id)) track.id = generateId("gd");
        byPath.emplace(track.audioPath, m_tracks.size());
        m_trackIndex[track.id] = m_tracks.size();
        m_tracks.push_back(std::move(track));
        ++added;
    }

    if (added || changed) markDirty();
    if (added || changed || notifyWhenUnchanged) notifyChanged();
    return added;
}

void MenuMusicLibrary::updateTrack(const MusicTrack& track) {
    if (auto* existing = findTrack(track.id)) {
        *existing = track;
        markDirty();
        notifyChanged();
    }
}

bool MenuMusicLibrary::deleteLocalAudio(const std::string& id) {
    auto* track = findTrack(id);
    if (!track || track->audioPath.empty()) return false;

    std::error_code ec;
    if (track->source == TrackSource::Downloaded) {
        std::filesystem::remove(track->audioPath, ec);
    } else if (track->source == TrackSource::GeometryDash) {
        auto stem = paimon::paths::toUtf8(
            std::filesystem::path(track->audioPath).stem());
        int songId = 0;
        auto [end, parseError] = std::from_chars(
            stem.data(), stem.data() + stem.size(), songId);
        if (parseError != std::errc{} || end != stem.data() + stem.size() || songId <= 0) {
            return false;
        }

        if (auto* manager = MusicDownloadManager::sharedState()) {
            manager->deleteSong(songId);
            if (std::filesystem::is_regular_file(track->audioPath, ec) && !ec) {
                std::filesystem::remove(track->audioPath, ec);
            }
        } else {
            std::filesystem::remove(track->audioPath, ec);
        }
    } else {
        return false;
    }

    if (ec) return false;
    notifyChanged();
    return true;
}

void MenuMusicLibrary::removeTrack(const std::string& id, bool deleteFiles) {
    auto* existing = findTrack(id);
    if (!existing) return;

    MusicTrack copy = *existing;

    m_tracks.erase(
        std::remove_if(m_tracks.begin(), m_tracks.end(),
            [&](const MusicTrack& t) { return t.id == id; }),
        m_tracks.end());
    rebuildTrackIndex();

    for (auto& pl : m_playlists) {
        pl.trackIds.erase(
            std::remove(pl.trackIds.begin(), pl.trackIds.end(), id),
            pl.trackIds.end());
    }

    if (m_lastTrackId == id) m_lastTrackId.clear();

    if (deleteFiles && copy.source == TrackSource::Downloaded) {
        std::error_code ec;
        if (!copy.audioPath.empty()) {
            std::filesystem::remove(copy.audioPath, ec);
        }
        if (!copy.coverPath.empty()) {
            std::filesystem::remove(copy.coverPath, ec);
        }
    }

    markDirty();
    notifyChanged();
}


MusicPlaylist* MenuMusicLibrary::findPlaylist(const std::string& id) {
    for (auto& p : m_playlists) if (p.id == id) return &p;
    return nullptr;
}

void MenuMusicLibrary::addPlaylist(const MusicPlaylist& pl) {
    m_playlists.push_back(pl);
    markDirty();
    notifyChanged();
}

void MenuMusicLibrary::renamePlaylist(const std::string& id, const std::string& newName) {
    if (auto* pl = findPlaylist(id)) {
        pl->name = newName;
        markDirty();
        notifyChanged();
    }
}

void MenuMusicLibrary::removePlaylist(const std::string& id) {
    m_playlists.erase(
        std::remove_if(m_playlists.begin(), m_playlists.end(),
            [&](const MusicPlaylist& p) { return p.id == id; }),
        m_playlists.end());
    if (m_activePlaylistId == id) m_activePlaylistId.clear();
    markDirty();
    notifyChanged();
}

void MenuMusicLibrary::addTrackToPlaylist(const std::string& playlistId, const std::string& trackId) {
    auto* pl = findPlaylist(playlistId);
    if (!pl) return;
    if (std::find(pl->trackIds.begin(), pl->trackIds.end(), trackId) != pl->trackIds.end()) return;
    pl->trackIds.push_back(trackId);
    markDirty();
    notifyChanged();
}

void MenuMusicLibrary::removeTrackFromPlaylist(const std::string& playlistId, const std::string& trackId) {
    auto* pl = findPlaylist(playlistId);
    if (!pl) return;
    auto before = pl->trackIds.size();
    pl->trackIds.erase(
        std::remove(pl->trackIds.begin(), pl->trackIds.end(), trackId),
        pl->trackIds.end());
    if (pl->trackIds.size() != before) {
        markDirty();
        notifyChanged();
    }
}


void MenuMusicLibrary::setMode(PlaybackMode mode) {
    if (m_mode == mode) return;
    m_mode = mode;
    markDirty();
    notifyChanged();
}

void MenuMusicLibrary::setActivePlaylistId(const std::string& id) {
    if (m_activePlaylistId == id) return;
    m_activePlaylistId = id;
    markDirty();
    notifyChanged();
}

void MenuMusicLibrary::setLastTrackId(const std::string& id) {
    if (m_lastTrackId == id) return;
    m_lastTrackId = id;
    markDirty();
}


std::string MenuMusicLibrary::generateId(const std::string& prefix) {
    m_idCounter++;
    auto stamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    return fmt::format("{}_{}_{}", prefix, m_idCounter, stamp);
}

bool MenuMusicLibrary::isAudioExtension(const std::filesystem::path& p) {
    auto ext = geode::utils::string::toLower(paimon::paths::toUtf8(p.extension()));
    // fmod 2.02+ decodes opus only in ogg; downloader remuxes on opus request.
    static const std::array<std::string, 7> ok = {
        ".mp3", ".ogg", ".wav", ".flac", ".oga", ".m4a", ".opus"
    };
    return std::find(ok.begin(), ok.end(), ext) != ok.end();
}

bool MenuMusicLibrary::isImageExtension(const std::filesystem::path& p) {
    auto ext = geode::utils::string::toLower(paimon::paths::toUtf8(p.extension()));
    static const std::array<std::string, 6> ok = {
        ".png", ".jpg", ".jpeg", ".webp", ".bmp", ".tiff"
    };
    return std::find(ok.begin(), ok.end(), ext) != ok.end();
}


std::size_t MenuMusicLibrary::addListener(Listener cb) {
    auto token = m_nextListenerToken++;
    m_listeners.emplace_back(token, std::move(cb));
    return token;
}

void MenuMusicLibrary::removeListener(std::size_t token) {
    m_listeners.erase(
        std::remove_if(m_listeners.begin(), m_listeners.end(),
            [&](const auto& p) { return p.first == token; }),
        m_listeners.end());
}

void MenuMusicLibrary::notifyChanged() {
    // defensive copy: a listener may unregister itself.
    auto copy = m_listeners;
    for (auto& [token, cb] : copy) {
        if (cb) cb();
    }
}


void MenuMusicLibrary::backupLastValidLibrary() {
    std::error_code ec;
    auto const src = getLibraryFile();
    if (!std::filesystem::is_regular_file(src, ec) || ec) return;
    auto bak = src;
    bak += ".bak";
    std::filesystem::copy_file(
        src, bak, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        log::warn("[MenuMusic] failed to back up library.json: {}", ec.message());
    }
}

void MenuMusicLibrary::save() {
    // never persist an empty library over a file we failed to read: without
    // this guard the next periodic save would wipe library.json (crash #7).
    if (m_loadFailed && m_tracks.empty() && m_playlists.empty()) {
        log::warn("[MenuMusic] refusing to save empty library after failed load");
        return;
    }

    if (m_loadFailed) {
        std::error_code ec;
        auto source = getLibraryFile();
        if (std::filesystem::exists(source, ec) && !ec) {
            auto corrupt = source;
            corrupt += ".corrupt";
            std::filesystem::rename(source, corrupt, ec);
            if (ec) {
                log::warn("[MenuMusic] could not preserve unreadable library: {}", ec.message());
                return;
            }
            log::warn("[MenuMusic] preserved unreadable library as {}", corrupt.filename().string());
        }
        m_loadFailed = false;
    }
    ensureDirs();

    auto root = matjson::Value::object();
    root["version"] = 2;
    root["mode"] = static_cast<int>(m_mode);
    root["activePlaylistId"] = m_activePlaylistId;
    root["lastTrackId"] = m_lastTrackId;
    root["idCounter"] = static_cast<std::int64_t>(m_idCounter);

    auto tracks = matjson::Value::array();
    for (const auto& t : m_tracks) {
        matjson::Value o = matjson::Value::object();
        o["id"] = t.id;
        o["audioPath"] = t.audioPath;
        o["coverPath"] = t.coverPath;
        o["displayName"] = t.displayName;
        o["artist"] = t.artist;
        o["sourceUrl"] = t.sourceUrl;
        o["source"] = static_cast<int>(t.source);
        o["addedUnixMs"] = t.addedUnixMs;
        o["durationMs"] = t.durationMs;
        o["favorite"] = t.favorite;
        o["blacklisted"] = t.blacklisted;
        tracks.push(o);
    }
    root["tracks"] = tracks;

    auto playlists = matjson::Value::array();
    for (const auto& p : m_playlists) {
        matjson::Value o = matjson::Value::object();
        o["id"] = p.id;
        o["name"] = p.name;
        o["createdUnixMs"] = p.createdUnixMs;
        auto ids = matjson::Value::array();
        for (const auto& tid : p.trackIds) ids.push(tid);
        o["trackIds"] = ids;
        playlists.push(o);
    }
    root["playlists"] = playlists;

    if (!paimon::file::writeAtomically(getLibraryFile(), root.dump())) {
        log::warn("[MenuMusic] failed to save library.json");
    }
}

void MenuMusicLibrary::load() {
    if (m_loaded) return;
    ensureDirs();

    std::error_code existsEc;
    if (!std::filesystem::exists(getLibraryFile(), existsEc) || existsEc) {
        m_loaded = true;
        m_loadFailed = false;
        save();
        return;
    }

    std::error_code sizeEc;
    auto const size = std::filesystem::file_size(getLibraryFile(), sizeEc);
    if (sizeEc || size > kMaxLibraryFileBytes) {
        // leave m_loaded false so a later load() retries; m_loadFailed blocks
        // save() from persisting the empty in-memory state over the file.
        m_loadFailed = true;
        log::warn("[MenuMusic] library.json is too large or unreadable; keeping it untouched");
        return;
    }

    auto res = geode::utils::file::readFromJson<matjson::Value>(paimon::paths::toSdkPath(getLibraryFile()));
    if (!res) {
        m_loadFailed = true;
        log::warn("[MenuMusic] failed to read library.json: {}; keeping it untouched", res.unwrapErr());
        return;
    }
    auto& root = res.unwrap();

    int rawMode = paimon::json::integerOr<int>(root["mode"], 0);
    m_mode = (rawMode >= 0 && rawMode <= static_cast<int>(PlaybackMode::Queue))
        ? static_cast<PlaybackMode>(rawMode) : PlaybackMode::Disabled;
    m_activePlaylistId = root["activePlaylistId"].asString().unwrapOr("");
    m_lastTrackId = root["lastTrackId"].asString().unwrapOr("");
    m_idCounter = static_cast<std::uint64_t>(
        std::max<std::int64_t>(0, paimon::json::integerOr<std::int64_t>(root["idCounter"], 0)));

    m_tracks.clear();
    if (auto arr = root["tracks"].asArray()) {
        for (const auto& item : arr.unwrap()) {
            MusicTrack t;
            t.id = item["id"].asString().unwrapOr("");
            if (t.id.empty()) continue;
            t.audioPath = item["audioPath"].asString().unwrapOr("");
            t.coverPath = item["coverPath"].asString().unwrapOr("");
            t.displayName = item["displayName"].asString().unwrapOr("");
            t.artist = item["artist"].asString().unwrapOr("");
            t.sourceUrl = item["sourceUrl"].asString().unwrapOr("");
            int rawSource = paimon::json::integerOr<int>(item["source"], 0);
            t.source = (rawSource >= 0 && rawSource <= static_cast<int>(TrackSource::GeometryDash))
                ? static_cast<TrackSource>(rawSource) : TrackSource::Unknown;
            t.addedUnixMs = paimon::json::integerOr<std::int64_t>(item["addedUnixMs"], 0);
            t.durationMs = static_cast<std::int32_t>(std::clamp<std::int64_t>(
                paimon::json::integerOr<std::int64_t>(item["durationMs"], 0), 0, INT32_MAX));
            t.favorite = item["favorite"].asBool().unwrapOr(false);
            t.blacklisted = item["blacklisted"].asBool().unwrapOr(false);
            m_tracks.push_back(std::move(t));
        }
    }

    m_playlists.clear();
    if (auto arr = root["playlists"].asArray()) {
        for (const auto& item : arr.unwrap()) {
            MusicPlaylist p;
            p.id = item["id"].asString().unwrapOr("");
            if (p.id.empty()) continue;
            p.name = item["name"].asString().unwrapOr("Untitled");
            p.createdUnixMs = paimon::json::integerOr<std::int64_t>(item["createdUnixMs"], 0);
            if (auto ids = item["trackIds"].asArray()) {
                for (const auto& it : ids.unwrap()) {
                    p.trackIds.push_back(it.asString().unwrapOr(""));
                }
            }
            m_playlists.push_back(std::move(p));
        }
    }

    rebuildTrackIndex();
    m_loaded = true;
    m_loadFailed = false;
    backupLastValidLibrary();
    repairDownloadedCoversAsync();

    log::info("[MenuMusic] library loaded: {} tracks, {} playlists, mode={}",
        m_tracks.size(), m_playlists.size(), static_cast<int>(m_mode));
}

} // namespace paimon::menumusic
